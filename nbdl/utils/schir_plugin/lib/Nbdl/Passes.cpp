#include <nbdl_spec/NbdlDialect.h>
#include <nbdl_spec/TranslateCpp.h>
#include <schir/SchirClang.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallString.h>
#include <mlir/IR/Dominance.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Transforms/Passes.h>
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
template <typename Base_>
class RewriteSchirClangBase : public Base_ {
  SchirClangMutex* SchirClangOpt;

public:
  using Base = RewriteSchirClangBase;

  template <typename ...Args>
  RewriteSchirClangBase(SchirClangMutex* SCM, Args&& ...args)
    : Base_(std::forward<Args>(args)...)
    , SchirClangOpt(SCM)
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
};

template <typename OpTy>
using OpRewriteSchirClang = RewriteSchirClangBase<mlir::OpRewritePattern<OpTy>>;

// Return true if V is a !nbdl.store has no resolved alternatives.
bool needsResolve(mlir::Value V) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(V.getType());
  return ST && ST.getAlts().empty();
}

mlir::Type getSingleAlt(mlir::Value V) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(V.getType());
  if (ST && ST.getAlts().size() == 1)
    return ST.getAlts().front().getValue();
  else
    return mlir::Type();
}

llvm::StringRef getSingleCppAlt(mlir::Value V) {
  auto ST = dyn_cast<nbdl_spec::StoreType>(V.getType());
  if (!ST || ST.getAlts().size() != 1)
    return {};
  mlir::Type T = getSingleAlt(V);

  auto CT = dyn_cast<nbdl_spec::CppType>(T);
  if (!CT)
    return {};
  return CT.getCppTypename();
};


// Resolve the result type of nbdl.visit.
struct InferVisitResultType : OpRewriteSchirClang<nbdl_spec::VisitOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(nbdl_spec::VisitOp Op,
                      mlir::PatternRewriter& Rewriter) const override {
    if (!HasSchirClang())
      return Rewriter.notifyMatchFailure(Op, "no SchirClang available");

    if (!needsResolve(Op.getResult()))
      return Rewriter.notifyMatchFailure(Op, "result type already resolved");

    if (llvm::any_of(Op.getArgs(), needsResolve))
      return Rewriter.notifyMatchFailure(Op, "args are not resolved");

    // If any argument isn't writeable as a single C++ type, fail.
    bool WriteExprFail = false;
    llvm::SmallString<128> Expr;
    llvm::raw_svector_ostream OS(Expr);
    writeVisitExpr(Op, OS);

    std::string Typename;

    auto [SCResult, ErrorMsg] = WithSchirClang(
      [&](schir::SchirClang SchirClang) {
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

    mlir::MLIRContext* Ctx = Op.getContext();
    auto NewCppT = mlir::TypeAttr::get(nbdl_spec::CppType::get(Ctx, Typename));
    auto NewStoreT = nbdl_spec::StoreType::get(Ctx, NewCppT);

    Rewriter.modifyOpInPlace(Op, [&] { Op.getResult().setType(NewStoreT); });
    return llvm::success();
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

    if (needsResolve(Cond))
      return Rewriter.notifyMatchFailure(Op, "cond type not yet resolved");

    llvm::StringRef CondTypeStr = getSingleCppAlt(Cond);
    if (CondTypeStr.empty())
      return Rewriter.notifyMatchFailure(Op,
                          "cond type not writeable as c++");

    mlir::Type NewThenArgT;

    // Implicitly unwrap a `sfinae_result`
    auto VOp = Cond.getDefiningOp<nbdl_spec::VisitOp>();
    if (VOp && VOp.getSfinae()) {
      CondTypeStr.consume_front("::");
      llvm::StringRef Prefix = "nbdl::detail::sfinae_result<";
      assert(CondTypeStr.starts_with(Prefix) &&
             CondTypeStr.ends_with(">") && "expecting sfinae_result");

      llvm::StringRef Inner = CondTypeStr.drop_front(Prefix.size()).drop_back(1);
      mlir::TypeAttr InnerCppT =
          mlir::TypeAttr::get(nbdl_spec::CppType::get(Op.getContext(), Inner));
      NewThenArgT = nbdl_spec::StoreType::get(Op.getContext(), InnerCppT);
    } else {
      NewThenArgT = Cond.getType();
    }

    Rewriter.modifyOpInPlace(Op, [&] { ThenArg.setType(NewThenArgT); });
    return llvm::success();
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
