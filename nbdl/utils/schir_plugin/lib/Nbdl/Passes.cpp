#include <nbdl_spec/NbdlDialect.h>
#include <nbdl_spec/TranslateCpp.h>
#include <schir/SchirClang.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Twine.h>
#include <mlir/AsmParser/AsmParser.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <memory>
#include <mutex>
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

bool needsResolveT(mlir::Type T) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(T);
  return ST && ST.getAlts().empty();
}

// Return true if V is a !nbdl.store has no resolved alternatives.
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

llvm::StringRef getSingleCppAltT(mlir::Type T) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(T);
  if (!ST || ST.getAlts().size() != 1)
    return {};

  mlir::Type AltT = getSingleAltT(T);
  if (auto CT = dyn_cast<nbdl_spec::CppType>(AltT))
    return CT.getCppTypename();
  else
    return {};
}

llvm::StringRef getSingleCppAlt(mlir::Value V) {
  return getSingleCppAltT(V.getType());
}

constexpr auto isCppWriteable = [](mlir::Value V) -> bool {
  mlir::Type T = V.getType();
  return isa<nbdl_spec::MemberNameType, nbdl_spec::FuncNameType>(T) ||
         !getSingleCppAlt(V).empty();
};

// Check that all type mappings are valid for a visit on a visible callee
// (ie in IR.)
// All call argument types should be resolved store types.
// mapping:
//  store<...> -> store
//  store<T> -> store<T>
//  store<cpp<"T">> -> store<{get_mlir_type<T>}>
//  store<cpp<"T">> -> {get_mlir_type<T>}
// where we abuse braces to indicate a mapped type via the
// expansion of a string to a parsed mlir type.
llvm::LogicalResult
RewriteSchirClangBaseBase::CheckVisitArgs(nbdl_spec::VisitOp Op,
                                          mlir::func::FuncOp CalleeFn) const {
  mlir::ValueRange Args = Op.getArgs();
  llvm::ArrayRef<mlir::Type> ParamTs = CalleeFn.getArgumentTypes();
  if (Args.size() != ParamTs.size()) {
    Op.emitError("invalid visit arity");
    return llvm::failure();
  }
  else if (Args.empty()) {
    return llvm::success();
  }

  // MappedArgTypes must satisfy the first two cases.
  llvm::SmallVector<mlir::Type, 8> MappedArgTypes;

  // If any (Arg -> Param) should map a CppType to a not CppType,
  // then all must be mapped via `get_mlir_type`.
  bool ShouldMapCppToMlir = false;
  for (auto [Arg, ParamT] : llvm::zip(Args, ParamTs)) {
    if (!getSingleCppAlt(Arg).empty() &&
        !needsResolveT(ParamT) &&
        getSingleCppAltT(ParamT).empty()) {
      ShouldMapCppToMlir = true;
      break;
    }
  }
  if (ShouldMapCppToMlir) {
    // Map every arg C++ type to a mlir::Type via nbdl::get_mlir_type
    // in the current C++ environment.

    // Map cpp type strings to mlir type strings. Allow nullptr.
    llvm::SmallVector<schir::String*, 8> MappedTypeStrs;
    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
        for (auto [Arg, ParamT] : llvm::zip(Args, ParamTs)) {
          llvm::StringRef CppTypeStr = getSingleCppAlt(Arg);
          llvm::SmallString<128> Expr("::nbdl::get_mlir_type<");
          Expr.append(CppTypeStr);
          Expr.append(">()");
          schir::SourceLocation Loc(mlir::OpaqueLoc
              ::getUnderlyingLocationOrNull<
                schir::SourceLocationEncoding*>(Op.getLoc()));
          auto* S = dyn_cast<schir::String>(SchirClang.ExprEval(Loc, Expr));
          MappedTypeStrs.push_back(S);
        }
      });
    for (schir::String* TypeStr : MappedTypeStrs) {
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
    if (ArgT != ParamT && !needsResolveT(ParamT)) {
      std::string Msg = ("Invalid visit mapping for argument " +
                         llvm::Twine(I)).str();
      Op.emitError(Msg);
      return llvm::failure();
    }
  }

  return llvm::success();
}

// Resolve the result type of nbdl.visit.
// Additionally, validate arguments if we have that in the IR
//  (ie when the callee is a FuncNameOp.)
struct InferVisitResultType : OpRewriteSchirClang<nbdl_spec::VisitOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(nbdl_spec::VisitOp Op,
                      mlir::PatternRewriter& Rewriter) const override {
    if (!HasSchirClang())
      return Rewriter.notifyMatchFailure(Op, "no SchirClang available");

    if (!needsResolve(Op.getResult()))
      return Rewriter.notifyMatchFailure(Op, "result type already resolved");

    if (needsResolve(Op.getFn()) ||
        llvm::any_of(Op.getArgs(), needsResolve))
      return Rewriter.notifyMatchFailure(Op, "args are not resolved");

    mlir::MLIRContext* Ctx = Op.getContext();
    nbdl_spec::StoreType NewStoreT;

    if (auto FN = Op.getFn().getDefiningOp<nbdl_spec::FuncNameOp>()) {
      // Look up the symbol and get the result type.
      auto M = Op->getParentOfType<mlir::ModuleOp>();
      mlir::Operation* Lookup = nullptr;
      if (M)
        Lookup = M.lookupSymbol(FN.getName());
      auto F = dyn_cast_or_null<mlir::func::FuncOp>(Lookup);
      llvm::ArrayRef<mlir::Type> ResultTs;
      if (F) {
        ResultTs = F.getResultTypes();
        // FIXME We will not get here for functions discarding their results
        //       (ie no result type deduction.)
        //       // We should move this check to [it's own] subsequent pass.
        // While we are here, validate the call arguments.
        if (llvm::failed(CheckVisitArgs(Op, F)))
          return llvm::failure();
      }
      if (ResultTs.size() == 1) {
        auto TA = mlir::TypeAttr::get(ResultTs.front());
        NewStoreT = nbdl_spec::StoreType::get(Ctx, TA);
      }
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

      auto NewCppT = mlir::TypeAttr::get(nbdl_spec::CppType::get(Ctx, Typename));
      NewStoreT = nbdl_spec::StoreType::get(Ctx, NewCppT);
    }

    if (NewStoreT) {
      Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewStoreT); });
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
    mlir::Type NewThenArgT = getSingleAlt(Cond);
    if (!NewThenArgT)
      return Rewriter.notifyMatchFailure(Op, "cond type not single alt");

    // Implicitly unwrap a `sfinae_result`
    llvm::StringRef CondTypeStr = getSingleCppAlt(Cond);
    auto VOp = Cond.getDefiningOp<nbdl_spec::VisitOp>();
    if (VOp && VOp.getSfinae() && !CondTypeStr.empty()) {
      CondTypeStr.consume_front("::");
      llvm::StringRef Prefix = "nbdl::detail::sfinae_result<";
      assert(CondTypeStr.starts_with(Prefix) &&
             CondTypeStr.ends_with(">") && "expecting sfinae_result");

      llvm::StringRef Inner = CondTypeStr.drop_front(Prefix.size()).drop_back(1);
      mlir::TypeAttr InnerCppT =
          mlir::TypeAttr::get(nbdl_spec::CppType::get(Op.getContext(), Inner));
      NewThenArgT = nbdl_spec::StoreType::get(Op.getContext(), InnerCppT);
    }

    Rewriter.modifyOpInPlace(Op, [&] { ThenArg.setType(NewThenArgT); });
    return llvm::success();
  }
};

struct InferMatchEachArgType
    : OpRewriteSchirClang<nbdl_spec::MatchEachOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
      nbdl_spec::MatchEachOp Op, mlir::PatternRewriter& Rewriter) const override {
    mlir::Value ElementArg = Op.getBody().getArgument(0);
    if (!needsResolve(ElementArg))
      return Rewriter.notifyMatchFailure(Op, "type already resolved");

    mlir::Value BeginArg = Op.getBegin();
    mlir::Type BeginArgT = getSingleAlt(BeginArg);
    if (!BeginArgT)
      return Rewriter.notifyMatchFailure(Op, "input not single alt");

    nbdl_spec::StoreType NewStoreT;

    // Handle cpp type.
    llvm::StringRef BeginTypeStr = getSingleCppAlt(BeginArg);
    if (!BeginTypeStr.empty()) {
      std::string Typename;
      std::string Expr = llvm::Twine("*(::nbdl::detail::declval<" +
                                     BeginTypeStr + ">())").str();
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
      mlir::TypeAttr InnerCppT =
          mlir::TypeAttr::get(
              nbdl_spec::CppType::get(Op.getContext(), Typename));
      NewStoreT = nbdl_spec::StoreType::get(Op.getContext(), InnerCppT);
    }

    if (NewStoreT) {
      Rewriter.modifyOpInPlace(Op, [&] { ElementArg.setType(NewStoreT); });
      return llvm::success();
    } else {
      return llvm::failure();
    }
  }
};

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

    Patterns = mlir::FrozenRewritePatternSet(std::move(PS));

    return llvm::success();
  }

  void runOnOperation() override {
    if (llvm::failed(run(getOperation())))
      signalPassFailure();
  }

  llvm::LogicalResult run(mlir::Operation* Op) {
    if (llvm::failed(mlir::applyPatternsGreedily(Op, Patterns)))
      return llvm::failure();
    return llvm::success();
  }
};

} // namespace

namespace nbdl_spec {

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
