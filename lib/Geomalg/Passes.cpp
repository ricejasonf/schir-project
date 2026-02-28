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
#define GEN_PASS_DEF_APPLYMETRICPASS
#define GEN_PASS_DEF_ARGUMENTDEDUCTIONPASS
#define GEN_PASS_DEF_TYPEINFERENCEPASS
#define GEN_PASS_DEF_EXPANDPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace {
using geomalg::isUnknown;
using geomalg::isZero;

class ApplyMetricPass
  : public geomalg::impl::ApplyMetricPassBase<ApplyMetricPass> {

  geomalg::Metric Metric;

public:
  ApplyMetricPass()
    : Metric(geomalg::Metric::get(geomalg::MetricKind::unknown))
  { }

  ApplyMetricPass(geomalg::ApplyMetricPassOptions Options)
    : Metric(geomalg::Metric::get(Options.metric))
  { }

  void runOnOperation() override;
};

class TypeInferencePass
  : public geomalg::impl::TypeInferencePassBase<TypeInferencePass> {

public:
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

// Rewrite the geometric product of blades as terms of inner and outer products.
struct ExpandGP : mlir::OpRewritePattern<geomalg::GeometricProductOp> {
  using Base = mlir::OpRewritePattern<geomalg::GeometricProductOp>;
  using Base::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(
      geomalg::GeometricProductOp GP,
      mlir::PatternRewriter& Rewriter) const override {
    mlir::Location Loc = GP->getLoc();
    mlir::Value LHS = GP.getLHS();
    mlir::Value RHS = GP.getRHS();
    auto L = dyn_cast<geomalg::BladeType>(GP.getLHS().getType());
    auto R = dyn_cast<geomalg::BladeType>(GP.getRHS().getType());
    if (!L || !R)
      return llvm::failure();

    // Use the left contraction for scalar multiplication.
    // α B = α ⌋ B
    if (L.getGrade() == 0) {
      Rewriter.replaceOpWithNewOp<geomalg::LeftContractionOp>(
        GP, R, LHS, RHS);
      return llvm::success();
    }

    // a B = a ⌋ B + a ∧ B
    if (L.getGrade() == 1) {
      mlir::Value NewLC = geomalg::LeftContractionOp::create(Rewriter, Loc,
                                                             LHS, RHS);
      mlir::Value NewWP = geomalg::WedgeOp::create(Rewriter, Loc, LHS, RHS);
      Rewriter.replaceOpWithNewOp<geomalg::SumOp>(GP, NewLC, NewWP);
      return llvm::success();
    }

    // Factor the LHS blade using
    // a ∧ B = 1/2 (a B + B^ a)
    auto [Type_a, Type_B] = L.factor();
    mlir::Value a = geomalg::BladeOp::create(Rewriter, Loc, Type_a);
    mlir::Value B = geomalg::CastOp::create(Rewriter, Loc, Type_B, LHS);
    mlir::Value Half = geomalg::BladeOp::create(Rewriter, Loc, Type_a, 0.5f);
    mlir::Value B_invo;
    if (Type_B.shouldInvoNegate())
      B_invo = geomalg::NegateOp::create(Rewriter, Loc, Type_B, B);
    mlir::Value GP1 = geomalg::GeometricProductOp::create(Rewriter, Loc, a, B);
    mlir::Value GP2 = geomalg::GeometricProductOp::create(Rewriter, Loc,
                                                          B_invo, a);
    mlir::Value Sum = geomalg::SumOp::create(Rewriter, Loc, GP1, GP2);
    // Use the left contraction for scalar multiplication.
    mlir::Value NewLC = geomalg::LeftContractionOp::create(Rewriter, Loc,
                                                           Half, Sum);
    Rewriter.replaceOpWithNewOp<geomalg::GeometricProductOp>(GP, NewLC, RHS);
    
    return llvm::failure();
  }
};

// Expand the left contraction to a multivector where appropriate
// by the GA4CS definitions. Note that the conventions are greek
// letters for scalars, lowercase letters for 1-blades and uppercase
// letters for arbitrary k-blades where we sometimes specify constraints on k.

// Any operation that does not expand implies multiplication of coefficients
// via commutativity of multiplication across the contraction.
// Result types are !geomalg.unknown when a metric is required.
struct ExpandLC : mlir::OpRewritePattern<geomalg::LeftContractionOp> {
  using Base = mlir::OpRewritePattern<geomalg::LeftContractionOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setHasBoundedRewriteRecursion();
  }

  void setResultType(mlir::PatternRewriter& Rewriter,
                     geomalg::LeftContractionOp LC, mlir::Type Type) const {
    Rewriter.startOpModification(LC.getOperation());
    LC.getResult().setType(Type);
    Rewriter.finalizeOpModification(LC.getOperation());
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::LeftContractionOp LC,
      mlir::PatternRewriter& Rewriter) const override {
    mlir::Location Loc = LC.getLoc();
    mlir::Value LHS = LC.getLHS();
    mlir::Value RHS = LC.getRHS();
    auto L = dyn_cast<geomalg::BladeType>(LC.getLHS().getType());
    auto R = dyn_cast<geomalg::BladeType>(LC.getRHS().getType());
    if (!L || !R)
      return llvm::failure();

    // 3.8
    // B ⌋ α = 0
    if (R.getGrade() == 0 && L.getGrade() > 0) {
      setResultType(Rewriter, LC, geomalg::ZeroType::get(LC->getContext()));
      return llvm::success();
    }

    // 3.7
    // α ⌋ B = α B
    if (L.getGrade() == 0) {
      setResultType(Rewriter, LC, R);
      return llvm::success();
    }

    if (L.getGrade() == 1 && R.getGrade() == 1) {
      // 3.9
      // a ⌋ b = a ⬝ b
      // Requires a metric the application of
      // which is deferred to its own pass.
      return llvm::failure();
    }

    // 3.10
    // (a ⌋ (b ∧ C) = ((a ⌋ b) ∧ C) - (b ∧ (a ⌋ C)))
    if (L.getGrade() == 1 && R.getGrade() > 1) {
      // Factor the RHS blade.
      // a ⌋ (b ∧ C) = ((a ⌋ b) ∧ C) + ((a ⌋ C) ∧ b)
      // Note we used the antisymmetric property for the second term
      auto [Type_b, Type_C] = R.factor();
      mlir::Value a = LHS;
      mlir::Value b = geomalg::BladeOp::create(Rewriter, Loc, Type_b);
      mlir::Value C = geomalg::CastOp::create(Rewriter, Loc, Type_C, RHS);
      // (a ⌋ b)
      mlir::Value ab = geomalg::LeftContractionOp::create(Rewriter, Loc, a, b);
      // (a ⌋ C)
      mlir::Value aC = geomalg::LeftContractionOp::create(Rewriter, Loc, a, C);
      // ((a ⌋ b) ∧ C)
      mlir::Value abC = geomalg::WedgeOp::create(Rewriter, Loc, ab, C);
      // ((a ⌋ C) ∧ b)
      mlir::Value aCb = geomalg::WedgeOp::create(Rewriter, Loc, aC, b);
      // ((a ⌋ b) ∧ C) + ((a ⌋ C) ∧ b)
      Rewriter.replaceOpWithNewOp<geomalg::SumOp>(RHS.getDefiningOp(),
                                                  abC, aCb);

      return llvm::success();
    }

    // 3.11
    if (L.getGrade() > 1 && R.getGrade()) {
      // Factor the LHS blade.
      // (a ∧ B) ⌋ C = a ⌋ (B ⌋ C)
      auto [Type_a, Type_B] = L.factor();
      auto a = geomalg::BladeOp::create(Rewriter, Loc, Type_a);
      auto B = geomalg::CastOp::create(Rewriter, Loc, Type_B, LHS);
      auto C = RHS;
      // (B ⌋ C)
      auto BC = geomalg::LeftContractionOp::create(Rewriter, Loc, B, C);
      // (a ⌋ (B ⌋ C))
      Rewriter.replaceOpWithNewOp<geomalg::LeftContractionOp>(
          LHS.getDefiningOp(), a, BC);

      return llvm::success();
    }

    return llvm::failure();
  }
};

struct ExpandInverse : mlir::OpRewritePattern<geomalg::InverseOp> {
  using Base = mlir::OpRewritePattern<geomalg::InverseOp>;
  using Base::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(
      geomalg::InverseOp InvOp,
      mlir::PatternRewriter& Rewriter) const override {
    mlir::MLIRContext* Ctx = getContext();
    mlir::Location Loc = InvOp.getLoc();
    mlir::Value Arg = InvOp.getArg();

    auto BT = dyn_cast<geomalg::BladeType>(Arg.getType());

    // Scalars will be lowered to 1/Arg. (ie division.)
    if (BT && BT.getGrade() == 0)
      return llvm::failure();

    // For blades we can simplify to a Negate since we know the grade.
    // For multivectors, Reverse will be distributed to each blade.

    mlir::Value Reverse;
    if (BT)
      Reverse = BT.shouldReverseNegate()
        ? geomalg::NegateOp::create(Rewriter, Loc, BT, Arg)
        : Arg;
    else
      Reverse = geomalg::ReverseOp::create(Rewriter, Loc, Arg);

    // The result is either scalar or unknown.
    mlir::Type LCResultType = BT ? mlir::Type(geomalg::BladeType::get(Ctx, 0))
                                 : mlir::Type(geomalg::UnknownType::get(Ctx));
    mlir::Value Squared = geomalg::LeftContractionOp
      ::create(Rewriter, Loc, LCResultType, Arg, Arg);
    mlir::Value SquareInverse = geomalg::InverseOp
      ::create(Rewriter, Loc, LCResultType, Squared);
    // Multiply the inverse of norm squared and reverse blade.
    Rewriter.replaceOpWithNewOp<geomalg::LeftContractionOp>(
        InvOp, SquareInverse, Reverse);
    return llvm::success();
  }
};

struct ExpandReverse : mlir::OpRewritePattern<geomalg::ReverseOp> {
  using Base = mlir::OpRewritePattern<geomalg::ReverseOp>;
  using Base::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(
      geomalg::ReverseOp RevOp,
      mlir::PatternRewriter& Rewriter) const override {
    mlir::MLIRContext* Ctx = getContext();
    mlir::Location Loc = RevOp.getLoc();
    mlir::Value Arg = RevOp.getArg();

    auto BT = dyn_cast<geomalg::BladeType>(Arg.getType());

    // Scalars will be lowered to 1/Arg. (ie division.)
    if (BT && BT.getGrade() == 0)
      return llvm::failure();

    // TODO FINISH and fix the notion of negative blade types
    //      (ie by removing said notion)

    return llvm::success();
  }
};
}  // namespace

void ExpandPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<Distribute,
         ExpandLC,
         ExpandGP,
         ExpandInverse,
         ExpandReverse>(Ctx);

  if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, std::move(PS))))
    return signalPassFailure();
}

namespace {
struct ApplyMetric : mlir::OpRewritePattern<geomalg::LeftContractionOp> {
  using Base = mlir::OpRewritePattern<geomalg::LeftContractionOp>;

  geomalg::Metric Metric;

  ApplyMetric(mlir::MLIRContext* Ctx, geomalg::Metric M)
    : Base(Ctx)
    , Metric(M)
  { }

  llvm::LogicalResult matchAndRewrite(
      geomalg::LeftContractionOp LC,
      mlir::PatternRewriter& Rewriter) const override {
    if (!Metric)
      return llvm::failure();

    mlir::Location Loc = LC.getLoc();
    mlir::Value LHS = LC.getLHS();
    mlir::Value RHS = LC.getRHS();
    auto L = dyn_cast<geomalg::BladeType>(LC.getLHS().getType());
    auto R = dyn_cast<geomalg::BladeType>(LC.getRHS().getType());
    if (!(L && L.getGrade() == 1 && R && R.getGrade() == 1))
      return llvm::failure();

    int DotResult = Metric.dotProduct(geomalg::BladeTag(L.getTag()),
                                      geomalg::BladeTag(L.getTag()));

    auto BT = geomalg::BladeType(0); // Scalar
    Rewriter.replaceOpWithNewOp<geomalg::BladeOp>(LC, BT, DotResult);
    return llvm::success();
  }
};
}  // namespace

void ApplyMetricPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<ApplyMetric>(Ctx, Metric);

  if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, std::move(PS))))
    return signalPassFailure();
}
