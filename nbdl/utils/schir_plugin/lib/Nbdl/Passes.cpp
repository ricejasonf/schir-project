#include <schir/SchirClang.h>
#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/Dominance.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Transforms/Passes.h>

// Generated stuff
namespace nbdl_spec {
#define GEN_PASS_DEF_FLATTENPASS
#include "nbdl_spec/NbdlPasses.h.inc"
}

namespace {
//TODO Implement PatterRewrite for inferring the result type of VisitOp.

class FlattenPass : public nbdl_spec::impl::FlattenPassBase<FlattenPass> {
  using Base = nbdl_spec::impl::FlattenPassBase<FlattenPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;

  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    mlir::RewritePatternSet PS(Ctx);

    // TODO populate PS

    Patterns = mlir::FrozenRewritePatternSet(std::move(PS));

    return llvm::success();
  }

  void runOnOperation() override {
    if (llvm::failed(run(getOperation())))
      signalPassFailure();
  }

  llvm::LogicalResult run(mlir::func::FuncOp FuncOp) {
    mlir::MLIRContext* Ctx = FuncOp->getContext();

    if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, Patterns)))
      return llvm::failure();
    return llvm::success();
  }
};

} // namespace

namespace nbdl_spec {

void runFlattenPass(
    schir::SchirClang SchirClangOpt,
    mlir::func::FuncOp FuncOp) {
  // TODO Run the pass.
}

} // namespace nbdl_spec
