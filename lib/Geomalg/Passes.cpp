#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <cassert>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_ARGUMENTDEDUCTIONPASS
#define GEN_PASS_DEF_TYPEINFERENCEPASS
#define GEN_PASS_DEF_EXPANDPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace {
using geomalg::isUnknown;
using geomalg::isZero;

class TypeInferencePass
  : public geomalg::impl::TypeInferencePassBase<TypeInferencePass> {

  geomalg::Metric Metric;

public:
  TypeInferencePass()
    : Metric(geomalg::Metric::get(geomalg::MetricKind::unknown))
  { }

  TypeInferencePass(geomalg::TypeInferencePassOptions Options)
    : Metric(geomalg::Metric::get(Options.metric))
  { }

  void runOnOperation() override;
};

struct ArgumentDeductionPass
  : public geomalg::impl::ArgumentDeductionPassBase<ArgumentDeductionPass> {
  void runOnOperation() override;
};

struct ExpandPass
  : public geomalg::impl::ExpandPassBase<ExpandPass> {
  void runOnOperation() override;
};

}  // namespace

void TypeInferencePass::runOnOperation() {
  mlir::func::FuncOp FuncOp = getOperation();
  // Assume Body has a single block.
  mlir::Block* Body = !FuncOp.getBody().empty()
    ? &FuncOp.getBody().front() : nullptr;

  mlir::func::ReturnOp ReturnOp;
  if (Body)
    ReturnOp = dyn_cast_if_present<mlir::func::ReturnOp>(Body->getTerminator());

  if (!ReturnOp || ReturnOp.getOperands().size() != 1 ||
      FuncOp.getResultTypes().size() != 1) {
    FuncOp.emitOpError("expecting single return type for function");
    return;
  }

  // At this point Body and ReturnOp are valid.

  // SumOp

  FuncOp.walk([](geomalg::SumOp SumOp) {
    if (!isUnknown(SumOp.getResult()))
      return mlir::WalkResult::advance();

    llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
    for (mlir::Value V : SumOp.getArgs()) {
      mlir::Type Type = V.getType();
      if (isUnknown(V))
        return mlir::WalkResult::advance();
      if (auto BT = dyn_cast<geomalg::BladeType>(Type))
        BladeTypes.push_back(BT);
      else if (auto MT = dyn_cast<geomalg::MultivectorType>(Type))
        llvm::append_range(BladeTypes, MT.getBlades());
      else
        assert(isZero(Type) &&
            "expecting a valid operand type to geomalg.sum");
    }

    mlir::Type NewType = createMultivectorType(BladeTypes);
    SumOp.getResult().setType(NewType);
    return mlir::WalkResult::advance();
  });

  // Finally infer the function return type by the operand
  // of the ReturnOp.
  mlir::Type OrigResultTy = FuncOp.getResultTypes().front();
  mlir::Type ReturnTy = ReturnOp.getOperands().front().getType();
  if (isUnknown(OrigResultTy) && !isUnknown(ReturnTy)) {
    // Replace the function type.
    mlir::FunctionType NewFT = mlir::FunctionType::get(
        FuncOp.getContext(), FuncOp.getArgumentTypes(), ReturnTy);
    FuncOp.setFunctionType(NewFT);
  }

}

void ArgumentDeductionPass::runOnOperation() {
  mlir::ModuleOp ModuleOp = getOperation();
  llvm::errs() << "\nTODO\n";
}

// Pattern rewriters
namespace {
struct Distribute : mlir::OpTraitRewritePattern<geomalg::Distributive> {
  using Base = mlir::OpTraitRewritePattern<geomalg::Distributive>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    // We unwrap multivector operands until we cannot find any more.
    setHasBoundedRewriteRecursion();
  }

  // The real meat and potatoes.
  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op, mlir::PatternRewriter& Rewriter) const override {
    // Given an operation whose operator distributes over addition,
    // match the first Multivector operand.
    auto Itr = llvm::find_if(Op->getOperands(), [](mlir::Value Operand) {
        return isa<geomalg::MultivectorType>(Operand.getType());
      });

    if (Itr == Op->getOperands().end())
      return llvm::failure();

    // The multivector we want to expand and distribute over.
    mlir::Value MV = *Itr;
    auto MVT = llvm::cast<geomalg::MultivectorType>(MV.getType());
    llvm::SmallVector<mlir::Type, 8> BladeTypes(MVT.getBlades());
    auto ExpandOp = geomalg::ExpandOp::create(Rewriter, Op->getLoc(),
                                              BladeTypes, MV);
    auto Blades = ExpandOp.getResults();

    // Apply the operator to each blade summing the results.
    llvm::SmallVector<mlir::Value, 8> Results;
    for (auto Blade : Blades) {
      // Clone the original operation mapping MV to Blade so
      // it replaces it as an operand.
      mlir::IRMapping Map;
      Map.map(MV, Blade);
      mlir::Operation* NewOp = Rewriter.cloneWithoutRegions(*Op, Map);
      Results.push_back(NewOp->getResult(0));
    }

    // Replace the original op with a shiny, new SumOp.
    Rewriter.replaceOpWithNewOp<geomalg::SumOp>(Op,
      geomalg::UnknownType(), Results);

    return llvm::success();
  }
};
} // namespace

void ExpandPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<Distribute>(Ctx);

  if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, std::move(PS))))
    return signalPassFailure();
}
