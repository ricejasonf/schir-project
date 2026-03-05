#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <cassert>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_FUNCINSTPASS
#define GEN_PASS_DEF_EXPANDPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace geomalg::transforms {
#include "geomalg/Transforms/Expand.h.inc"
}

namespace {
using geomalg::isUnknown;
using geomalg::isZero;

class ExpandPass : public geomalg::impl::ExpandPassBase<ExpandPass> {
  geomalg::Metric Metric;

public:
  ExpandPass()
    : Metric(geomalg::Metric::get(geomalg::MetricKind::unknown))
  { }

  ExpandPass(geomalg::ExpandPassOptions Options)
    : Metric(geomalg::Metric::get(Options.metric))
  { }

  void runOnOperation() override;
};

struct FuncInstPass : public geomalg::impl::FuncInstPassBase<FuncInstPass> {
  void runOnOperation() override;
};

// Rewriters for type inference.
class InferFuncType
  : public mlir::OpRewritePattern<mlir::func::FuncOp> {
  using Base = mlir::OpRewritePattern<mlir::func::FuncOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("InferFuncType");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::func::FuncOp, mlir::PatternRewriter&) const override;
};


// Rewriters for expansion.

// Distribute multilinear operations across the elements of a multivector
// where SumOp is the base case.
// (ie Addition is multilinear but does not distribute over itself.
//     This is my own way of justifying it in my mind so far.)
struct Distribute : mlir::OpTraitRewritePattern<geomalg::Distributive> {
  using Base = mlir::OpTraitRewritePattern<geomalg::Distributive>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    // TODO Do we want recursion here?
    //setHasBoundedRewriteRecursion();
    setDebugName("Distribute");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op, mlir::PatternRewriter& Rewriter) const override;
};

// Rewrite the geometric product of blades as terms of inner and outer products.
struct ExpandGP : mlir::OpRewritePattern<geomalg::GeomProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::GeomProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandGP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::GeomProdOp GP,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the inner product as the left contraction to a multivector.
struct ExpandLC : mlir::OpRewritePattern<geomalg::InnerProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::InnerProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    // TODO Do we want recursion here?
    //setHasBoundedRewriteRecursion();
    setDebugName("ExpandGP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::InnerProdOp LC,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the inverse to the reverse multiplied
// by the inverse of its norm squared.
struct ExpandInverse : mlir::OpRewritePattern<geomalg::InverseOp> {
  using Base = mlir::OpRewritePattern<geomalg::InverseOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandInverse");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::InverseOp InvOp,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the reverse of a blade to a negate or the identity
// based on its grade.
struct ExpandReverse : mlir::OpRewritePattern<geomalg::ReverseOp> {
  using Base = mlir::OpRewritePattern<geomalg::ReverseOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandReverse");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::ReverseOp RevOp,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand inner product of basis 1-blades.
// If the metric is unspecified (unknown) this does nothing.
struct ApplyMetric : mlir::OpRewritePattern<geomalg::InnerProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::InnerProdOp>;

  geomalg::Metric Metric;

  ApplyMetric(mlir::MLIRContext* Ctx, geomalg::Metric M)
    : Base(Ctx)
    , Metric(M)
  { }

  void initialize() {
    setDebugName("ApplyMetric");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::InnerProdOp LC,
      mlir::PatternRewriter& Rewriter) const override;
};

}  // namespace

static void setResultType(mlir::PatternRewriter& Rewriter,
                   mlir::Operation* Op, mlir::Type Type) {
  Rewriter.startOpModification(Op);
  Op->getResult(0).setType(Type);
  Rewriter.finalizeOpModification(Op);
}

llvm::LogicalResult InferFuncType::matchAndRewrite(
    mlir::func::FuncOp FuncOp,
    mlir::PatternRewriter& Rewriter) const {
  // Assume Body has a single block.
  mlir::Block* Body = !FuncOp.getBody().empty()
    ? &FuncOp.getBody().front() : nullptr;

  geomalg::ReturnOp ReturnOp;
  if (Body)
    ReturnOp = dyn_cast_if_present<geomalg::ReturnOp>(Body->getTerminator());

  if (!ReturnOp || ReturnOp.getOperands().size() != 1 ||
      FuncOp.getResultTypes().size() != 1) {
    return llvm::failure();
  }

  // Finally infer the function return type by the operand
  // of the ReturnOp.
  mlir::Type OrigResultTy = FuncOp.getResultTypes().front();
  mlir::Type ReturnTy = ReturnOp.getOperands().front().getType();
  if (isUnknown(OrigResultTy) && !isUnknown(ReturnTy)) {
    // Replace the function type.
    mlir::FunctionType NewFT = mlir::FunctionType::get(
        FuncOp.getContext(), FuncOp.getArgumentTypes(), ReturnTy);
    Rewriter.startOpModification(FuncOp);
    FuncOp.setFunctionType(NewFT);
    Rewriter.finalizeOpModification(FuncOp);
  }
  return llvm::success();
}

// The real meat and potatoes.
llvm::LogicalResult Distribute::matchAndRewrite(
    mlir::Operation* Op, mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();

  // Match any Zero operand.
  bool HasZero = llvm::any_of(Op->getOperands(), [](mlir::Value Operand) {
      return isa<geomalg::ZeroType>(Operand.getType());
    });
  if (HasZero) {
    // Replace entire operation with zero sum.
    Rewriter.replaceOpWithNewOp<geomalg::SumOp>(Op);
    return llvm::success();
  }

  // Match the first Multivector operand.
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
    // The NewOp result type is the same as its operand or it must
    // be inferred via !geomalg.unknown.
    mlir::Value Result = NewOp->getResult(0);
    mlir::Type ResultT;
    if (NewOp->hasTrait<mlir::OpTrait::SameOperandsAndResultType>())
      ResultT = NewOp->getOperand(0).getType();
    else
      ResultT = geomalg::UnknownType::get(Ctx);
    Result.setType(ResultT);  // ok because we are modifying a new result.
    Results.push_back(Result);
  }

  // Replace the original op with a shiny, new SumOp.
  Rewriter.replaceOpWithNewOp<geomalg::SumOp>(Op, Results);

  return llvm::success();
}

// Rewrite the geometric product of blades as terms of inner and outer products.
llvm::LogicalResult ExpandGP::matchAndRewrite(
    geomalg::GeomProdOp GP,
    mlir::PatternRewriter& Rewriter) const {
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
    Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(
      GP, R, LHS, RHS);
    return llvm::success();
  }

  // a B = a ⌋ B + a ∧ B
  if (L.getGrade() == 1) {
    mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, LHS, RHS);
    mlir::Value NewWP = geomalg::OuterProdOp::create(Rewriter, Loc, LHS, RHS);
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
  mlir::Value GP1 = geomalg::GeomProdOp::create(Rewriter, Loc, a, B);
  mlir::Value GP2 = geomalg::GeomProdOp::create(Rewriter, Loc, B_invo, a);
  mlir::Value Sum = geomalg::SumOp::create(Rewriter, Loc, GP1, GP2);
  // Use the left contraction for scalar multiplication.
  mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, Half, Sum);
  Rewriter.replaceOpWithNewOp<geomalg::GeomProdOp>(GP, NewLC, RHS);

  return llvm::failure();
}

// Expand the left contraction to a multivector where appropriate
// by the GA4CS definitions. Note that the conventions are greek
// letters for scalars, lowercase letters for 1-blades and uppercase
// letters for arbitrary k-blades where we sometimes specify constraints on k.

// Any operation that does not expand implies multiplication of coefficients
// via commutativity of multiplication across the contraction.
// Result types are !geomalg.unknown when a metric is required.
llvm::LogicalResult ExpandLC::matchAndRewrite(
    geomalg::InnerProdOp LC,
    mlir::PatternRewriter& Rewriter) const {
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
    mlir::Value ab = geomalg::InnerProdOp::create(Rewriter, Loc, a, b);
    // (a ⌋ C)
    mlir::Value aC = geomalg::InnerProdOp::create(Rewriter, Loc, a, C);
    // ((a ⌋ b) ∧ C)
    mlir::Value abC = geomalg::OuterProdOp::create(Rewriter, Loc, ab, C);
    // ((a ⌋ C) ∧ b)
    mlir::Value aCb = geomalg::OuterProdOp::create(Rewriter, Loc, aC, b);
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
    auto BC = geomalg::InnerProdOp::create(Rewriter, Loc, B, C);
    // (a ⌋ (B ⌋ C))
    Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(
        LHS.getDefiningOp(), a, BC);

    return llvm::success();
  }

  return llvm::failure();
}

llvm::LogicalResult ExpandInverse::matchAndRewrite(
    geomalg::InverseOp InvOp,
    mlir::PatternRewriter& Rewriter) const {
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
  mlir::Value Squared = geomalg::InnerProdOp
    ::create(Rewriter, Loc, LCResultType, Arg, Arg);
  mlir::Value SquareInverse = geomalg::InverseOp
    ::create(Rewriter, Loc, LCResultType, Squared);
  // Multiply the inverse of norm squared and reverse blade.
  Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(
      InvOp, SquareInverse, Reverse);
  return llvm::success();
}

llvm::LogicalResult ExpandReverse::matchAndRewrite(
    geomalg::ReverseOp RevOp,
    mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  mlir::Location Loc = RevOp.getLoc();
  mlir::Value Arg = RevOp.getArg();

  auto BT = dyn_cast<geomalg::BladeType>(Arg.getType());

  // Multivectors are handled by the Distribute rewriter.
  if (!BT)
    return llvm::failure();

  if (BT.shouldReverseNegate())
    Rewriter.replaceOpWithNewOp<geomalg::NegateOp>(RevOp, RevOp.getArg());
  else
    Rewriter.replaceOp(RevOp, RevOp.getArg());

  return llvm::success();
}

llvm::LogicalResult ApplyMetric::matchAndRewrite(
    geomalg::InnerProdOp LC,
    mlir::PatternRewriter& Rewriter) const {
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

void ExpandPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
#if 0
  PS.add<Distribute,
         ExpandLC,
         ExpandGP,
         ExpandInverse,
         ExpandReverse>(Ctx);
  PS.add<ApplyMetric>(Ctx, Metric);
#endif
  geomalg::transforms::populateGeneratedPDLLPatterns(PS);

  if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, std::move(PS))))
    return signalPassFailure();
}

void FuncInstPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::ModuleOp ModuleOp = getOperation();

  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<InferFuncType>(Ctx);

  if (llvm::failed(mlir::applyPatternsGreedily(ModuleOp, std::move(PS))))
    return signalPassFailure();
}
