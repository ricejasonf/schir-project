#include <nbdl_spec/NbdlDialect.h>
#include <nbdl_spec/TranslateCpp.h>
#include <schir/MappableToCpp.h>
#include <schir/SchirClang.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Twine.h>
#include <mlir/AsmParser/AsmParser.h>
#include <mlir/IR/AttrTypeSubElements.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

// Generated stuff
namespace nbdl_spec {
#define GEN_PASS_DEF_FLATTENPASS
#include "nbdl_spec/NbdlPasses.h.inc"
}

using llvm::dyn_cast;

namespace {
// Prevent patterns from concurrently accessing Clang.
struct SchirClangMutex {
  std::mutex Mutex;
  schir::SchirClangImpl* Impl;

  SchirClangMutex(schir::SchirClangImpl* Impl)
    : Impl(Impl)
  { }
};

// Enable some patterns to use our Clang integration
// to perform introspection on C++.
class RewriteSchirClangBaseBase {
  SchirClangMutex* SchirClangOpt;

protected:
  RewriteSchirClangBaseBase(SchirClangMutex* SCM)
    : SchirClangOpt(SCM)
  { }

  bool HasSchirClang() const {
    return static_cast<bool>(SchirClangOpt);
  }

  std::pair<llvm::LogicalResult, std::string>
  WithSchirClang(llvm::function_ref<void(schir::SchirClang)> Fn) const {
    if (!HasSchirClang())
      return {llvm::failure(), "no SchirClang instance"};

    auto& [Mutex, Impl] = *SchirClangOpt;
    std::lock_guard LG(Mutex);
    schir::SchirClang SchirClang(Impl);
    Fn(SchirClang);
    if (SchirClang.HasError())
      return {llvm::failure(), SchirClang.ErrorMsg};
    else
      return {llvm::success(), {}};
  }

  llvm::LogicalResult CheckVisitArgs(nbdl_spec::VisitOp Op,
                                     mlir::func::FuncOp CalleeFn) const;
};

template <typename Base_>
class RewriteSchirClangBase : public Base_,
                              protected RewriteSchirClangBaseBase {
public:
  using Base = RewriteSchirClangBase;

  template <typename ...Args>
  RewriteSchirClangBase(SchirClangMutex* SCM, Args&& ...args)
    : Base_(std::forward<Args>(args)...)
    , RewriteSchirClangBaseBase(SCM)
  { }
};

template <typename OpTy>
using OpRewriteSchirClang = RewriteSchirClangBase<mlir::OpRewritePattern<OpTy>>;

// Return true if T is !nbdl.unknown or a !nbdl.store
// with no resolved alternatives.
bool needsResolveT(mlir::Type T) {
  if (isa<nbdl_spec::UnknownType>(T))
    return true;
  auto ST = dyn_cast<nbdl_spec::StoreType>(T);
  return ST && ST.getAlts().empty();
}

bool needsResolve(mlir::Value V) {
  return needsResolveT(V.getType());
}

mlir::Type getSingleAltT(mlir::Type T) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(T);
  if (ST && ST.getAlts().size() == 1)
    return ST.getAlts().front().getValue();
  else
    return mlir::Type();
}

mlir::Type getSingleAlt(mlir::Value V) {
  return getSingleAltT(V.getType());
}

// Get the C++ typename of a C++ type or a store
// with a single C++ alternative.
llvm::StringRef getCppTypenameT(mlir::Type T) {
  if (auto ST = dyn_cast<nbdl_spec::StoreType>(T))
    T = getSingleAltT(ST);
  if (auto CT = dyn_cast_or_null<nbdl_spec::CppType>(T))
    return CT.getCppTypename();
  else
    return {};
}

llvm::StringRef getCppTypename(mlir::Value V) {
  return getCppTypenameT(V.getType());
}

constexpr auto isCppWriteable = [](mlir::Value V) -> bool {
  mlir::Type T = V.getType();
  return isa<nbdl_spec::MemberNameType, nbdl_spec::FuncNameType>(T) ||
         !getCppTypename(V).empty();
};

// Check visit argument types and inline any calls
// on FuncOps with store type params or lower to
// raw func.call.
struct InlineVisit : OpRewriteSchirClang<nbdl_spec::VisitOp> {
  using Base::Base;

  // Check that all type mappings are valid for a visit on a visible callee
  // (ie in IR.)
  // All call argument types should be resolved store types or map to an
  // unresolved store.
  // mapping:
  //  store<...> -> store
  //  store<T> -> store<T>
  //  store<T> -> T
  //  T -> store<T>
  //  store<cpp<"T">> -> store<{get_mlir_type<T>}>
  //  store<cpp<"T">> -> {get_mlir_type<T>}
  // where we abuse braces to indicate a mapped type via the
  // expansion of a string to a parsed mlir type.
  llvm::LogicalResult matchAndRewrite(nbdl_spec::VisitOp Op,
                      mlir::PatternRewriter& Rewriter) const override {
    if (Op.getValidCppCrossMap())
      return Rewriter.notifyMatchFailure(Op, "cpp mapping already certified");
    if (Op.getSfinae())
      return Rewriter.notifyMatchFailure(Op, "sfinae visit is not inlined");

    mlir::func::FuncOp CalleeFn = Op.lookupCallee();
    if (!CalleeFn)
      return Rewriter.notifyMatchFailure(Op, "callee is not a func");

    mlir::ValueRange Args = Op.getArgs();
    llvm::ArrayRef<mlir::Type> ParamTs = CalleeFn.getArgumentTypes();
    if (Args.size() != ParamTs.size()) {
      Op.emitError("invalid visit arity");
      return llvm::failure();
    }

    // Arguments should be resolved or map to an unresolved store.
    for (auto [Arg, ParamT] : llvm::zip(Args, ParamTs))
      if (needsResolve(Arg) && !needsResolveT(ParamT))
        return Rewriter.notifyMatchFailure(Op, "args are not resolved");

    // MappedArgTypes must satisfy the first two cases.
    llvm::SmallVector<mlir::Type, 8> MappedArgTypes;

    bool IsCppToMlir = shouldMapCppToMlir(Args, ParamTs);
    if (IsCppToMlir) {
      // Map every arg C++ type to a mlir::Type via nbdl::get_mlir_type
      // in the current C++ environment.

      // Map cpp type strings to mlir type strings. Allow nullptr.
      llvm::SmallVector<schir::String*, 8> MappedTypeStrs;
      auto [SCResult, ErrorMsg] = WithSchirClang(
        [&](schir::SchirClang SchirClang) {
          for (auto [Arg, ParamT] : llvm::zip(Args, ParamTs)) {
            llvm::StringRef CppTypeStr = getCppTypename(Arg);
            if (CppTypeStr.empty()) {
              // This is an invalid case handled in isValidMapping.
              MappedTypeStrs.push_back(nullptr);
              continue;
            }
            llvm::SmallString<128> Expr("::nbdl::detail::mlir_type_name<");
            Expr.append(CppTypeStr);
            Expr.append(">()");
            schir::SourceLocation Loc(mlir::OpaqueLoc
                ::getUnderlyingLocationOrNull<
                  schir::SourceLocationEncoding*>(Op.getLoc()));
            auto* S = dyn_cast_or_null<schir::String>(
                SchirClang.ExprEval(Loc, Expr));
            MappedTypeStrs.push_back(S);
          }
        });
      if (llvm::failed(SCResult)) {
        Op.emitError("clang get_mlir_type evaluation failed: " + ErrorMsg);
        return llvm::failure();
      }
      for (auto [Arg, TypeStr] : llvm::zip(Args, MappedTypeStrs)) {
        if (getCppTypename(Arg).empty()) {
          MappedArgTypes.push_back(Arg.getType());
          continue;
        }
        mlir::MLIRContext* Ctx = Op.getContext();
        mlir::Type ParsedT;
        if (TypeStr && !TypeStr->getStringRef().empty())
          ParsedT = mlir::parseType(TypeStr->getStringRef(),
                                    Ctx, nullptr,
                                    schir::String::IsNullTerminated);
        MappedArgTypes.push_back(ParsedT);
      }
    } else {
      for (mlir::Value Arg : Args)
        MappedArgTypes.push_back(Arg.getType());
    }

    // Check that the types are equal or map to a placeholder.
    assert(MappedArgTypes.size() == Args.size());
    for (auto [I, ArgT, ParamT] : llvm::enumerate(MappedArgTypes, ParamTs)) {
      if (!isValidMapping(ArgT, ParamT)) {
        std::string Msg = ("Invalid visit mapping for argument " +
                           llvm::Twine(I)).str();
        Op.emitError(Msg);
        return llvm::failure();
      }
    }

    if (IsCppToMlir) {
      // Add an attribute to certify the cpp to mlir mappings
      // since that information is not available in the IR.
      Rewriter.modifyOpInPlace(Op, [&] { Op.setValidCppCrossMap(true); });
    } else if (llvm::any_of(ParamTs,
          [](mlir::Type T) { return isa<nbdl_spec::StoreType>(T); })) {
      // Inline the function visit call.
      return inlineVisit(Op, CalleeFn, Rewriter);
    } else {
      // Store types should "unwrap" to their contained type.
      return lowerToCall(Op, CalleeFn, Rewriter);
    }

    return llvm::success();
  }

  // Given an argument type (possibly mapped from C++) check
  // that it is valid for the parameter type.
  static bool isValidMapping(mlir::Type ArgT, mlir::Type ParamT) {
    if (!ArgT)
      return false;
    if (ArgT == ParamT || needsResolveT(ParamT))
      return true;
    // store<T> -> T
    if (!isa<nbdl_spec::StoreType>(ParamT) && getSingleAltT(ArgT) == ParamT)
      return true;
    // {mlir} -> store<{mlir}>
    if (!isa<nbdl_spec::StoreType>(ArgT) && getSingleAltT(ParamT) == ArgT)
      return true;
    return false;
  }

  // Get the discard op if the result of the visit is discarded
  // as the terminator of its block (ie it is in tail position.)
  static nbdl_spec::DiscardOp getTailDiscard(nbdl_spec::VisitOp Op) {
    mlir::Value Result = Op.getResult();
    if (!Result.hasOneUse())
      return {};
    auto Discard = dyn_cast<nbdl_spec::DiscardOp>(*Result.user_begin());
    if (!Discard || Discard->getBlock() != Op->getBlock() ||
        Discard->getNextNode() != nullptr)
      return {};
    return Discard;
  }

  // Replace the tail call to a function with store type
  // params with the body of that function.
  llvm::LogicalResult inlineVisit(nbdl_spec::VisitOp Op,
                                  mlir::func::FuncOp CalleeFn,
                                  mlir::PatternRewriter& Rewriter) const {
    if (CalleeFn.isExternal())
      return Rewriter.notifyMatchFailure(Op, "callee has no body");
    if (!CalleeFn.getBody().hasOneBlock())
      return Rewriter.notifyMatchFailure(Op, "callee has multiple blocks");
    if (CalleeFn->isAncestor(Op))
      return Rewriter.notifyMatchFailure(Op, "recursive visit");
    if (CalleeFn.getNumResults() != 0)
      return Rewriter.notifyMatchFailure(Op, "callee has results");
    nbdl_spec::DiscardOp Discard = getTailDiscard(Op);
    if (!Discard)
      return Rewriter.notifyMatchFailure(Op, "visit result is not discarded");

    mlir::Block& CalleeBody = CalleeFn.getBody().front();
    mlir::IRMapping Mapping;
    Rewriter.setInsertionPoint(Discard);

    // Lift arguments that are not stores for store type parameters.
    for (auto [Param, Arg] : llvm::zip(CalleeBody.getArguments(),
                                       Op.getArgs())) {
      mlir::Value Mapped = Arg;
      if (isa<nbdl_spec::StoreType>(Param.getType()) &&
          !isa<nbdl_spec::StoreType>(Arg.getType())) {
        // An unknown argument lifts to an unresolved store.
        auto LiftedT = needsResolve(Arg)
          ? nbdl_spec::StoreType::get(Op.getContext())
          : nbdl_spec::StoreType::get(Op.getContext(),
                                      mlir::TypeAttr::get(Arg.getType()));
        Mapped = nbdl_spec::LiftStoreOp::create(Rewriter, Arg.getLoc(),
                                                LiftedT, Arg);
      }
      Mapping.map(Param, Mapped);
    }

    // The callee body has its own terminator so it
    // replaces both the visit and the discard.
    for (mlir::Operation& CalleeOp : CalleeBody)
      Rewriter.clone(CalleeOp, Mapping);

    Rewriter.eraseOp(Discard);
    Rewriter.eraseOp(Op);
    return llvm::success();
  }

  // Lower to a func.call where arguments that are stores
  // are unwrapped to their contained type.
  llvm::LogicalResult lowerToCall(nbdl_spec::VisitOp Op,
                                  mlir::func::FuncOp CalleeFn,
                                  mlir::PatternRewriter& Rewriter) const {
    mlir::Value Result = Op.getResult();
    bool IsDiscarded = llvm::all_of(Result.getUsers(),
        [](mlir::Operation* User) { return isa<nbdl_spec::DiscardOp>(User); });
    if (!IsDiscarded)
      return Rewriter.notifyMatchFailure(Op,
          "lowering visit with used result is not supported");

    llvm::SmallVector<mlir::Value, 8> CallArgs;
    for (auto [Arg, ParamT] : llvm::zip(Op.getArgs(),
                                        CalleeFn.getArgumentTypes())) {
      if (Arg.getType() == ParamT)
        CallArgs.push_back(Arg);
      else
        CallArgs.push_back(nbdl_spec::UnwrapOp::create(
              Rewriter, Arg.getLoc(), ParamT, Arg));
    }

    mlir::func::CallOp::create(Rewriter, Op.getLoc(), CalleeFn, CallArgs);
    Rewriter.replaceOpWithNewOp<nbdl_spec::UnitOp>(Op,
        nbdl_spec::UnitType::get(Op.getContext()));
    return llvm::success();
  }

  // If any argument is resolved as a c++ type and maps to a non-cpp
  // type, indicate that validating the mappings is necessary.
  bool shouldMapCppToMlir(mlir::ValueRange Args,
                          mlir::TypeRange ParamTs) const {
    // If any (Arg -> Param) should map a CppType to a not CppType,
    // then all must be mapped via `get_mlir_type`.
    bool Result = false;
    for (auto [Arg, ParamT] : llvm::zip(Args, ParamTs)) {
      if (!getCppTypename(Arg).empty() &&
          !needsResolveT(ParamT) &&
          getCppTypenameT(ParamT).empty()) {
        Result = true;
        break;
      }
    }
    return Result;
  }
};

// Resolve the result type of nbdl.visit.
// Additionally, validate arguments if we have that in the IR
//  (ie when the callee is a function name.)
struct InferVisitResultType : OpRewriteSchirClang<nbdl_spec::VisitOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(nbdl_spec::VisitOp Op,
                      mlir::PatternRewriter& Rewriter) const override {
    if (!HasSchirClang())
      return Rewriter.notifyMatchFailure(Op, "no SchirClang available");

    if (!isa<nbdl_spec::UnknownType>(Op.getType()))
      return Rewriter.notifyMatchFailure(Op, "result type already resolved");

    if (needsResolve(Op.getFn()) ||
        llvm::any_of(Op.getArgs(), needsResolve))
      return Rewriter.notifyMatchFailure(Op, "args are not resolved");

    mlir::MLIRContext* Ctx = Op.getContext();
    mlir::Type NewT;

    if (mlir::func::FuncOp F = Op.lookupCallee()) {
      llvm::ArrayRef<mlir::Type> ResultTs = F.getResultTypes();
      if (ResultTs.size() == 1 && !needsResolveT(ResultTs.front()))
        NewT = ResultTs.front();
    } else if (llvm::all_of(Op.getArgs(), isCppWriteable)) {
      // All arguments are writeable as C++.
      std::string Typename;
      llvm::SmallString<128> Expr;
      llvm::raw_svector_ostream OS(Expr);

      // This only generates the text for the expr.
      auto [WriteResult, _] = writeVisitExpr(Op, OS);
      if (llvm::failed(WriteResult)) {
        Op.emitError("clang write visit expr failed");
        return llvm::failure();
      }

      auto [SCResult, ErrorMsg] = WithSchirClang(
        [&](schir::SchirClang SchirClang) {
          // TODO Loc could be hoisted from here.
          schir::SourceLocation Loc(mlir::OpaqueLoc
              ::getUnderlyingLocationOrNull<
                schir::SourceLocationEncoding*>(Op.getLoc()));
          Typename = SchirClang.ExprType(Loc, Expr);
        });
      if (llvm::failed(SCResult)) {
        Op.emitError("clang expr type introspection failed");
        return llvm::failure();
      } else if (Typename.empty()) {
        Op.emitError("clang expr type yielded empty string");
        return llvm::failure();
      } else if (Typename.starts_with('<')) {
        Op.emitError("clang expr type yielded placeholder: " + Typename);
        return llvm::failure();
      }

      NewT = nbdl_spec::CppType::get(Ctx, Typename);
    }

    if (NewT) {
      Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewT); });
      return llvm::success();
    } else {
      return llvm::failure();
    }
  }
};

// Infer the C++ type of the match_if thenRegion block argument.
struct InferMatchIfThenArgType
    : mlir::OpRewritePattern<nbdl_spec::MatchIfOp> {
  using mlir::OpRewritePattern<nbdl_spec::MatchIfOp>::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::MatchIfOp Op, mlir::PatternRewriter& Rewriter) const override {
    mlir::Value Cond = Op.getCond();
    mlir::Value ThenArg = Op.getThenRegion().getArgument(0);

    if (!needsResolve(ThenArg))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");

    // Other than special cases (e.g. sfinae), the ThenArg should
    // be the result of the conditional expression.
    mlir::Type NewThenArgT = Cond.getType();
    if (needsResolveT(NewThenArgT))
      return Rewriter.notifyMatchFailure(Op, "cond type not resolved");

    // Implicitly unwrap a `sfinae_result`
    llvm::StringRef CondTypeStr = getCppTypename(Cond);
    auto VOp = Cond.getDefiningOp<nbdl_spec::VisitOp>();
    if (VOp && VOp.getSfinae() && !CondTypeStr.empty()) {
      CondTypeStr.consume_front("::");
      llvm::StringRef Prefix = "nbdl::detail::sfinae_result<";
      assert(CondTypeStr.starts_with(Prefix) &&
             CondTypeStr.ends_with(">") && "expecting sfinae_result");

      llvm::StringRef Inner = CondTypeStr.drop_front(Prefix.size()).drop_back(1);
      NewThenArgT = nbdl_spec::CppType::get(Op.getContext(), Inner);
    }

    Rewriter.modifyOpInPlace(Op, [&] { ThenArg.setType(NewThenArgT); });
    return llvm::success();
  }
};

// Infer the type of the match_each element block argument
// from the range which is a store with a single alternative.
//  store<cpp<"R">> -> store<cpp<{range element type of R}>>
//  store<memref<...xT>> -> store<T>
struct InferMatchEachArgType
    : OpRewriteSchirClang<nbdl_spec::MatchEachOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::MatchEachOp Op, mlir::PatternRewriter& Rewriter) const override {
    mlir::Value ElementArg = Op.getBody().getArgument(0);
    if (!needsResolve(ElementArg))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");

    mlir::Value Range = Op.getRange();
    if (needsResolve(Range))
      return Rewriter.notifyMatchFailure(Op, "input not resolved");

    mlir::Type RangeT = getSingleAlt(Range);
    if (!RangeT)
      return Rewriter.notifyMatchFailure(Op, "range is not single alt");

    mlir::Type ElementT;
    if (auto MRT = dyn_cast<mlir::MemRefType>(RangeT)) {
      ElementT = MRT.getElementType();
    } else if (llvm::StringRef RangeTypeStr = getCppTypename(Range);
               !RangeTypeStr.empty()) {
      std::string Typename;
      std::string Expr = ("*::std::ranges::begin(::nbdl::detail::declval<" +
                          RangeTypeStr + "&>())").str();
      schir::SourceLocation Loc(mlir::OpaqueLoc
          ::getUnderlyingLocationOrNull<
            schir::SourceLocationEncoding*>(Op.getLoc()));
      auto [SCResult, ErrorMsg] = WithSchirClang(
        [&](schir::SchirClang SchirClang) {
          Typename = SchirClang.ExprType(Loc, Expr);
        });
      if (llvm::failed(SCResult)) {
        Op.emitError("clang expr type introspection failed");
        return llvm::failure();
      } else if (Typename.empty()) {
        Op.emitError("clang expr type yielded empty string");
        return llvm::failure();
      }
      ElementT = nbdl_spec::CppType::get(Op.getContext(), Typename);
    } else {
      Op.emitError("unsupported range type for match_each: ") << RangeT;
      return llvm::failure();
    }

    auto NewStoreT = nbdl_spec::StoreType::get(Op.getContext(),
                                               mlir::TypeAttr::get(ElementT));
    Rewriter.modifyOpInPlace(Op, [&] { ElementArg.setType(NewStoreT); });
    return llvm::success();
  }
};

// Infer the result of lifting a value whose type is inferred.
struct InferLiftStoreType : mlir::OpRewritePattern<nbdl_spec::LiftStoreOp> {
  using mlir::OpRewritePattern<nbdl_spec::LiftStoreOp>::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::LiftStoreOp Op,
      mlir::PatternRewriter& Rewriter) const override {
    if (!needsResolve(Op.getResult()))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");
    mlir::Type ValueT = Op.getValue().getType();
    if (needsResolveT(ValueT))
      return Rewriter.notifyMatchFailure(Op, "value type not resolved");

    auto NewStoreT = nbdl_spec::StoreType::get(Op.getContext(),
                                               mlir::TypeAttr::get(ValueT));
    Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewStoreT); });
    return llvm::success();
  }
};

// Infer the result of getting a member from a store.
// Any other key (or no key) uses `nbdl::get` as in the C++ translation.
struct InferGetType : OpRewriteSchirClang<nbdl_spec::GetOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::GetOp Op, mlir::PatternRewriter& Rewriter) const override {
    if (!needsResolve(Op.getResult()))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");
    llvm::StringRef StoreTypeStr = getCppTypename(Op.getState());
    if (StoreTypeStr.empty())
      return Rewriter.notifyMatchFailure(Op, "store is not a single C++ alt");

    std::string StoreExpr = ("::nbdl::detail::declval<" +
                             StoreTypeStr + ">()").str();
    std::string Expr;
    if (Op.hasUnitKey()) {
      Expr = "::nbdl::get(" + StoreExpr + ")";
    } else if (auto MemberName =
                   Op.getKey().getDefiningOp<nbdl_spec::MemberNameOp>()) {
      Expr = (llvm::Twine(StoreExpr) + "." + MemberName.getName()).str();
    } else {
      llvm::StringRef KeyTypeStr = getCppTypename(Op.getKey());
      if (KeyTypeStr.empty())
        return Rewriter.notifyMatchFailure(Op, "key is not a single C++ alt");
      Expr = ("::nbdl::get(" + llvm::Twine(StoreExpr) +
              ", ::nbdl::detail::declval<" + KeyTypeStr + ">())").str();
    }

    std::string Typename;
    schir::SourceLocation Loc(mlir::OpaqueLoc
        ::getUnderlyingLocationOrNull<
          schir::SourceLocationEncoding*>(Op.getLoc()));
    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
        Typename = SchirClang.ExprType(Loc, Expr);
      });
    if (llvm::failed(SCResult)) {
      Op.emitError("clang expr type introspection failed");
      return llvm::failure();
    } else if (Typename.empty()) {
      Op.emitError("clang expr type yielded empty string");
      return llvm::failure();
    }

    mlir::MLIRContext* Ctx = Op.getContext();
    auto NewStoreT = nbdl_spec::StoreType::get(Ctx,
        mlir::TypeAttr::get(nbdl_spec::CppType::get(Ctx, Typename)));
    Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewStoreT); });
    return llvm::success();
  }
};

// Infer the type of the block argument of the last overload of a match
// when it is the only unresolved overload (ie the catch all overload.)
struct InferMatchOverloadType : OpRewriteSchirClang<nbdl_spec::MatchOp> {
  using Base::Base;

  // Each probe requires a unique template name.
  static inline unsigned ProbeId = 0;

  // Get the C++ typenames of every alternative of a store
  // returning false if any are not C++ types.
  static bool getCppAlts(mlir::Value V,
                         llvm::SmallVectorImpl<llvm::StringRef>& Results) {
    auto ST = dyn_cast<nbdl_spec::StoreType>(V.getType());
    if (!ST || ST.getAlts().empty())
      return false;
    for (mlir::TypeAttr TA : ST.getAlts()) {
      auto CT = dyn_cast<nbdl_spec::CppType>(TA.getValue());
      if (!CT)
        return false;
      Results.push_back(CT.getCppTypename());
    }
    return true;
  }

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::MatchOp Op, mlir::PatternRewriter& Rewriter) const override {
    mlir::MutableArrayRef<mlir::Region> Overloads = Op.getOverloads();
    if (Overloads.empty())
      return Rewriter.notifyMatchFailure(Op, "match has no overloads");
    mlir::Region& Overload = Overloads.back();
    if (Overload.empty() || Overload.getNumArguments() != 1)
      return Rewriter.notifyMatchFailure(Op, "expecting a unary overload");
    mlir::BlockArgument Arg = Overload.getArgument(0);
    if (!needsResolve(Arg))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");

    // The alternatives handled by the previous overloads
    // which must all be resolved C++ types.
    llvm::SmallVector<llvm::StringRef, 4> HandledAlts;
    for (mlir::Region& Prev : Overloads.drop_back()) {
      if (Prev.empty() || Prev.getNumArguments() != 1 ||
          !getCppAlts(Prev.getArgument(0), HandledAlts))
        return Rewriter.notifyMatchFailure(Op,
            "previous overloads are not resolved C++ types");
    }

    llvm::SmallVector<llvm::StringRef, 4> StoreAlts;
    if (!getCppAlts(Op.getStore(), StoreAlts))
      return Rewriter.notifyMatchFailure(Op, "store alts not resolved");
    // An empty key string denotes the unit key.
    llvm::SmallVector<llvm::StringRef, 4> KeyAlts;
    if (Op.hasUnitKey())
      KeyAlts.push_back({});
    else if (!getCppAlts(Op.getKey(), KeyAlts))
      return Rewriter.notifyMatchFailure(Op, "key alts not resolved");

    schir::SourceLocation Loc(mlir::OpaqueLoc
        ::getUnderlyingLocationOrNull<
          schir::SourceLocationEncoding*>(Op.getLoc()));
    llvm::SmallVector<std::string, 4> Alts;
    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
        for (llvm::StringRef StoreAlt : StoreAlts) {
          for (llvm::StringRef KeyAlt : KeyAlts) {
            std::string ProbeName = ("::nbdl::detail::probe<" +
                                     llvm::Twine(++ProbeId) +
                                     ">::apply").str();
            std::string Expr;
            llvm::raw_string_ostream OS(Expr);
            OS << "::nbdl::match(::nbdl::detail::declval<" << StoreAlt
               << "&>(), ";
            if (!KeyAlt.empty())
              OS << "::nbdl::detail::declval<" << KeyAlt << ">(), ";
            OS << "[](auto&& ... args) -> void { (void)" << ProbeName
               << "<std::remove_cvref_t<decltype(args)>...>(); })";
            llvm::SmallVector<std::vector<std::string>, 4> Results;
            SchirClang.TemplateProbe(Results, Loc, ProbeName, Expr);
            if (SchirClang.HasError())
              return;
            for (std::vector<std::string>& Result : Results)
              for (std::string& Alt : Result)
                if (!llvm::is_contained(Alts, Alt) &&
                    !llvm::is_contained(HandledAlts, Alt))
                  Alts.push_back(std::move(Alt));
          }
        }
      });
    if (llvm::failed(SCResult)) {
      Op.emitError("clang match probe failed: " + ErrorMsg);
      return llvm::failure();
    }
    // Leave an unreachable overload unresolved.
    if (Alts.empty())
      return Rewriter.notifyMatchFailure(Op, "no alternatives remain");

    mlir::MLIRContext* Ctx = Op.getContext();
    llvm::SmallVector<mlir::TypeAttr, 4> AltAttrs;
    for (llvm::StringRef Alt : Alts)
      AltAttrs.push_back(mlir::TypeAttr::get(
            nbdl_spec::CppType::get(Ctx, Alt)));
    auto NewStoreT = nbdl_spec::StoreType::get(Ctx, AltAttrs);
    Rewriter.modifyOpInPlace(Op, [&] { Arg.setType(NewStoreT); });
    return llvm::success();
  }
};

// Infer the C++ type of the result of a constant expression.
struct InferConstexprType : OpRewriteSchirClang<nbdl_spec::ConstexprOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::ConstexprOp Op,
      mlir::PatternRewriter& Rewriter) const override {
    if (!isa<nbdl_spec::UnknownType>(Op.getType()))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");

    std::string Typename;
    schir::SourceLocation Loc(mlir::OpaqueLoc
        ::getUnderlyingLocationOrNull<
          schir::SourceLocationEncoding*>(Op.getLoc()));
    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
        Typename = SchirClang.ExprType(Loc, Op.getExpr());
      });
    if (llvm::failed(SCResult)) {
      Op.emitError("clang expr type introspection failed");
      return llvm::failure();
    } else if (Typename.empty()) {
      Op.emitError("clang expr type yielded empty string");
      return llvm::failure();
    }

    auto NewT = nbdl_spec::CppType::get(Op.getContext(), Typename);
    Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewT); });
    return llvm::success();
  }
};

// Inline match that simplifies to the identity operation.
struct InlineMatch : OpRewriteSchirClang<nbdl_spec::MatchOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::MatchOp Op, mlir::PatternRewriter& Rewriter) const override {
    if (!Op.hasUnitKey())
      return Rewriter.notifyMatchFailure(Op, "match has a key");

    mlir::Value Store = Op.getStore();
    mlir::Type StoreAltT = getSingleAlt(Store);
    if (!StoreAltT)
      return Rewriter.notifyMatchFailure(Op, "store is not single alt");

    // Non-C++ types have no mechanism to implement match.
    llvm::StringRef CppTypeStr = getCppTypename(Store);
    if (!CppTypeStr.empty()) {
      bool IsIdentity = false;
      schir::SourceLocation Loc(mlir::OpaqueLoc
          ::getUnderlyingLocationOrNull<
            schir::SourceLocationEncoding*>(Op.getLoc()));
      auto [SCResult, ErrorMsg] = WithSchirClang(
        [&](schir::SchirClang SchirClang) {
          std::string Expr = ("!::nbdl::detail::HasMatchUnitImpl<" +
                              CppTypeStr + ">").str();
          IsIdentity = SchirClang.ExprEvalBool(Loc, Expr);
        });
      if (llvm::failed(SCResult))
        return Rewriter.notifyMatchFailure(Op, "clang evaluation failed: " +
                                               ErrorMsg);
      if (!IsIdentity)
        return Rewriter.notifyMatchFailure(Op, "store implements unit match");
    }

    mlir::Region* Selected = selectOverload(Op, StoreAltT);
    if (!Selected)
      return Rewriter.notifyMatchFailure(Op, "overload cannot be selected");

    Rewriter.inlineBlockBefore(&Selected->front(), Op, Store);
    Rewriter.eraseOp(Op);
    return llvm::success();
  }

  // Select the first overload that matches StoreAltT checking linearly.
  // Return nullptr if no overload matches.
  mlir::Region* selectOverload(nbdl_spec::MatchOp Op,
                               mlir::Type StoreAltT) const {
    // The canonical C++ typename of a literal type (e.g. i32)
    // computed lazily when compared to a C++ type.
    std::optional<std::string> LiteralCppTypename;
    for (mlir::Region& Overload : Op.getOverloads()) {
      auto ST = dyn_cast<nbdl_spec::StoreType>(
          Overload.getArgument(0).getType());
      if (!ST)
        return nullptr;
      if (ST.getAlts().empty())
        return &Overload;
      for (mlir::TypeAttr TA : ST.getAlts()) {
        mlir::Type AltT = TA.getValue();
        if (AltT == StoreAltT) {
          return &Overload;
        } else if (auto CT = dyn_cast<nbdl_spec::CppType>(AltT);
                   CT && isLiteralType(StoreAltT)) {
          // Match types of literals to corresponding c++ types.
          if (!LiteralCppTypename)
            LiteralCppTypename = getCanonicalCppTypename(Op, StoreAltT);
          if (!LiteralCppTypename->empty() &&
              CT.getCppTypename() == *LiteralCppTypename)
            return &Overload;
        }
      }
    }
    return nullptr;
  }

  // Literal types that may match their corresponding C++ types.
  static bool isLiteralType(mlir::Type T) {
    return T.isSignlessInteger(32) || T.isF32();
  }

  // Get the canonical C++ typename that T maps to
  // or an empty string if it cannot be determined.
  std::string getCanonicalCppTypename(mlir::Operation* Op,
                                      mlir::Type T) const {
    llvm::SmallString<32> Typename;
    if (!schir::MappableToCpp::lookup(T, Typename))
      return {};
    std::string Canonical;
    schir::SourceLocation Loc(mlir::OpaqueLoc
        ::getUnderlyingLocationOrNull<
          schir::SourceLocationEncoding*>(Op->getLoc()));
    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
        Canonical = SchirClang.ParseType(Loc, Typename);
      });
    if (llvm::failed(SCResult))
      return {};
    return Canonical;
  }
};

// Replace every !nbdl.cpp_alias (including nested types and attributes)
// with the !nbdl.cpp type of its canonical typename. The flatten pass
// does this before the patterns are applied so they may compare C++ types.
llvm::LogicalResult canonicalizeCppAliases(mlir::Operation* Root,
                                           SchirClangMutex& SCM) {
  // The operation being updated for diagnostics.
  mlir::Operation* CurOp = nullptr;
  bool HasError = false;
  mlir::AttrTypeReplacer Replacer;
  Replacer.addReplacement(
    [&](nbdl_spec::CppAliasType T) -> std::optional<mlir::Type> {
      schir::SourceLocation Loc(mlir::OpaqueLoc
          ::getUnderlyingLocationOrNull<
            schir::SourceLocationEncoding*>(CurOp->getLoc()));
      auto& [Mutex, Impl] = SCM;
      std::lock_guard LG(Mutex);
      schir::SchirClang SchirClang(Impl);
      std::string Canonical = SchirClang.ParseType(Loc, T.getCppTypename());
      if (SchirClang.HasError() || Canonical.empty()) {
        CurOp->emitError("clang type parsing failed for C++ typename: ")
          << T.getCppTypename();
        HasError = true;
        return std::nullopt;
      }
      return nbdl_spec::CppType::get(T.getContext(), Canonical);
    });

  Root->walk([&](mlir::Operation* Op) {
    CurOp = Op;
    Replacer.replaceElementsIn(Op, /*replaceAttrs=*/true,
                               /*replaceLocs=*/false,
                               /*replaceTypes=*/true);
  });
  return llvm::failure(HasError);
}

class FlattenPass : public nbdl_spec::impl::FlattenPassBase<FlattenPass> {
  using Base = nbdl_spec::impl::FlattenPassBase<FlattenPass>;
  mlir::FrozenRewritePatternSet Patterns;
  std::shared_ptr<SchirClangMutex> SchirClangOpt;

public:
  using Base::Base;

  explicit FlattenPass(std::shared_ptr<SchirClangMutex> SchirClangOpt)
    : SchirClangOpt(std::move(SchirClangOpt))
  { }

  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    mlir::RewritePatternSet PS(Ctx);

    PS.add<InferVisitResultType>(SchirClangOpt.get(), Ctx);
    PS.add<InferMatchEachArgType>(SchirClangOpt.get(), Ctx);
    PS.add<InferMatchIfThenArgType>(Ctx);
    PS.add<InferLiftStoreType>(Ctx);
    PS.add<InferGetType>(SchirClangOpt.get(), Ctx);
    PS.add<InferConstexprType>(SchirClangOpt.get(), Ctx);
    PS.add<InferMatchOverloadType>(SchirClangOpt.get(), Ctx);
    PS.add<InlineMatch>(SchirClangOpt.get(), Ctx);
    PS.add<InlineVisit>(SchirClangOpt.get(), Ctx,
                        mlir::PatternBenefit(100));

    Patterns = mlir::FrozenRewritePatternSet(std::move(PS));

    return llvm::success();
  }

  void runOnOperation() override {
    if (llvm::failed(run(getOperation())))
      signalPassFailure();
  }

  llvm::LogicalResult run(mlir::Operation* Op) {
    if (SchirClangOpt &&
        llvm::failed(canonicalizeCppAliases(Op, *SchirClangOpt)))
      return llvm::failure();
    if (llvm::failed(mlir::applyPatternsGreedily(Op, Patterns)))
      return llvm::failure();
    return llvm::success();
  }

};

} // namespace

namespace nbdl_spec {
#define GEN_PASS_DECL_LOWERPASS
#define GEN_PASS_DECL_LOWERTOLLVMPASS
#define GEN_PASS_REGISTRATION_LOWERPASS
#define GEN_PASS_REGISTRATION_LOWERTOLLVMPASS
#include "nbdl_spec/NbdlPasses.h.inc"

void registerPasses() {
  registerLowerPass();
  registerLowerToLLVMPass();
}

llvm::LogicalResult canonicalizeCppTypes(mlir::Operation* Op,
                            schir::SchirClangImpl* SchirClangImpl) {
  SchirClangMutex SCM(SchirClangImpl);
  return canonicalizeCppAliases(Op, SCM);
}

llvm::LogicalResult runFlattenPass(mlir::Operation* Op,
                            schir::SchirClangImpl* SchirClangImpl) {
  mlir::PassManager PM(Op->getContext());

  // The mutex wrapper will be necessary if we end
  // up using nested passes.
  auto SCM = SchirClangImpl ? std::make_shared<SchirClangMutex>(SchirClangImpl)
                            : std::shared_ptr<SchirClangMutex>();
  PM.addPass(std::make_unique<FlattenPass>(std::move(SCM)));
  return PM.run(Op);
}

} // namespace nbdl_spec
