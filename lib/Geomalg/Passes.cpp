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
#include <string>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_FUNCINSTPASS
#define GEN_PASS_DEF_EXPANDPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace geomalg::transforms {
#include "geomalg/Transforms/Expand.h.inc"
}

namespace geomalg {
// Because lldb cannot look at mlir::Operation, ...
void dumpBlock(mlir::Operation* Op) {
  Op->getBlock()->dump();
}

void dumpBlock(mlir::Value V) {
  V.getParentBlock()->dump();
}
}

// Expand a multivector for the purpose of expanding
// via distribution or unnesting of sums.
static mlir::ValueRange
expandMultivector(mlir::PatternRewriter& Rewriter, mlir::Value MV) {
  assert(isa<geomalg::MultivectorType>(MV.getType()) &&
      "expecting multivector type or unknown defined by sum");
  // Check if we can hijack the operands of the defining sum if any.
  if (geomalg::SumOp OrigSumOp = MV.getDefiningOp<geomalg::SumOp>())
    return OrigSumOp.getArgs();

  // Prevent duplicate ExpandOps.
  geomalg::ExpandOp ExpandOp;
  auto ExpandOpItr = llvm::find_if(MV.getUsers(),
      [](mlir::Operation* Op) {
        return llvm::isa<geomalg::ExpandOp>(Op);
      });
  if (ExpandOpItr != MV.getUsers().end()) {
    ExpandOp = cast<geomalg::ExpandOp>(*ExpandOpItr);
  } else {
    // Ensure SSA dominance by inserting as "early" as possible.
    mlir::OpBuilder::InsertionGuard IG(Rewriter);
    Rewriter.setInsertionPointAfterValue(MV);
    auto MVT = llvm::cast<geomalg::MultivectorType>(MV.getType());
    llvm::SmallVector<mlir::Type, 8> BladeTypes(MVT.getBlades());
    ExpandOp = geomalg::ExpandOp::create(Rewriter, MV.getLoc(),
                                         BladeTypes, MV);
  }
  return ExpandOp.getResults();
}

namespace {
using geomalg::isUnknown;
using geomalg::isZero;

class ExpandPass : public geomalg::impl::ExpandPassBase<ExpandPass> {
  using Base = geomalg::impl::ExpandPassBase<ExpandPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;
  void runOnOperation() override;
  llvm::LogicalResult initialize(mlir::MLIRContext*) override;
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

struct ExpandSum : mlir::OpRewritePattern<geomalg::SumOp> {
  using Base = mlir::OpRewritePattern<geomalg::SumOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setHasBoundedRewriteRecursion();
    setDebugName("ExpandSum");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::SumOp Op, mlir::PatternRewriter& Rewriter) const override;
};

// Distribute multilinear operations across the elements of a multivector
// where SumOp is the base case.
// (ie Addition is multilinear but does not distribute over itself.
//     This is my own way of justifying it in my mind so far.)
struct Distribute : mlir::OpTraitRewritePattern<geomalg::Distributive> {
  using Base = mlir::OpTraitRewritePattern<geomalg::Distributive>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    setDebugName("Distribute");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op, mlir::PatternRewriter& Rewriter) const override;
};

// If any operand is Zero, then replace the Op
// with something resulting in Zero.
struct ZeroAbsorbToZero : mlir::OpTraitRewritePattern<geomalg::ZeroAbsorb> {
  using Base = mlir::OpTraitRewritePattern<geomalg::ZeroAbsorb>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    setDebugName("ZeroAbsorbToZero");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op, mlir::PatternRewriter& Rewriter) const override;
};

struct UpdateInferredTypes
    : mlir::OpInterfaceRewritePattern<mlir::InferTypeOpInterface> {
  using Base = mlir::OpInterfaceRewritePattern<mlir::InferTypeOpInterface>;
  using Base::OpInterfaceRewritePattern;

  void initialize() {
    setDebugName("UpdateInferredTypes");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::InferTypeOpInterface Op,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifyOP : mlir::OpRewritePattern<geomalg::OuterProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::OuterProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyOP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::OuterProdOp OP,
      mlir::PatternRewriter& Rewriter) const override;
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

struct ExpandVP : mlir::OpRewritePattern<geomalg::VersorProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::VersorProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandVP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the inner product as the left contraction to a multivector.
struct ExpandLC : mlir::OpRewritePattern<geomalg::InnerProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::InnerProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandLC");
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

static llvm::LogicalResult
setResultType(mlir::PatternRewriter& Rewriter,
              mlir::Operation* Op, mlir::Type Type) {
  Rewriter.startOpModification(Op);
  Op->getResult(0).setType(Type);
  Rewriter.finalizeOpModification(Op);
  return llvm::success();
}

static llvm::LogicalResult
replaceWithZero(mlir::PatternRewriter& Rewriter,
                mlir::Operation* Op) {
  auto ZeroT = geomalg::ZeroType::get(Op->getContext());
  Rewriter.replaceOpWithNewOp<geomalg::BladeOp>(Op, ZeroT, 0);
  return llvm::success();
}

// Peel of a basis vector from a basis blade.
static std::pair<mlir::Value, mlir::Value>
factorBlade(mlir::PatternRewriter& Rewriter, mlir::Value V) {
  mlir::Location Loc = V.getLoc();
  auto BT = cast<geomalg::BladeType>(V.getType());
  if (!BT.isCanonical()) {
    BT = BT.getCanonicalType();
    V = geomalg::NegateOp::create(Rewriter, Loc, BT, V);
  }
  assert(BT.getGrade() > 1 && "prevent idempotent rewrite");
  auto [Type_a, Type_B] = BT.factor();
  mlir::Value a = geomalg::BladeOp::create(Rewriter, Loc, Type_a, 1.0);
  mlir::Value B = geomalg::CastOp::create(Rewriter, Loc, Type_B, V);
  return {a, B};
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

llvm::LogicalResult
ExpandSum::matchAndRewrite(geomalg::SumOp Op,
                           mlir::PatternRewriter& Rewriter) const {
  using SumOp = geomalg::SumOp;
  using BladeType = geomalg::BladeType;
  using MultivectorType = geomalg::MultivectorType;
  using UnknownType = geomalg::UnknownType;
  using ZeroType = geomalg::ZeroType;

  mlir::Location Loc = Op.getLoc();

  if (Op.getArgs().size() == 0)
    return replaceWithZero(Rewriter, Op);

  // Sum of single operand can be elided safely.
  if (Op.getArgs().size() == 1) {
    Rewriter.replaceOp(Op, Op.getArgs().front());
    return llvm::success();
  }

  // Replace unnecessary ExpandOps. It is necessary that the operands
  // represent the same values as the results they are replacing.
  bool DidReplaceAnyExpands = false;
  for (mlir::OpOperand& OO : Op.getResult().getUses()) {
    if (auto ExpandOp = dyn_cast<geomalg::ExpandOp>(OO.getOwner())) {
      Rewriter.replaceOp(ExpandOp, Op.getOperands());
      DidReplaceAnyExpands = true;
    }
  }

  if (DidReplaceAnyExpands)
    return llvm::success();

  // Collect Values unnesting (directly nested) sums,
  // negating noncanical blades, and discarding Zeros.
  // Note some of these maybe still be multivectors.
  llvm::SmallVector<mlir::Value, 8> Values;
  for (mlir::Value V : Op.getOperands()) {
    if (auto S = V.getDefiningOp<SumOp>()) {
      llvm::append_range(Values, S.getOperands());
    } else if (isa<MultivectorType>(V.getType())) {
      llvm::append_range(Values, expandMultivector(Rewriter, V));
    } else if (auto BT = dyn_cast<BladeType>(V.getType())) {
      if (!BT.isCanonical())
        V = geomalg::NegateOp::create(Rewriter, Loc, BT.getCanonicalType(), V);
      Values.push_back(V);
    } else if (isa<UnknownType>(V.getType())) {
      Values.push_back(V);
    } else {
      assert(isa<ZeroType>(V.getType()));
    }
  };

  // Sort by BladeTag putting all non-blades at the front.
  llvm::stable_sort(Values, [](mlir::Value Aval, mlir::Value Bval) {
    geomalg::BladeType A = dyn_cast<BladeType>(Aval.getType());
    geomalg::BladeType B = dyn_cast<BladeType>(Bval.getType());
    if (!A || !B)
      return B && !A;
    return A.getTag() < B.getTag();
  });
  if (llvm::equal(Values, Op.getOperands()))
    return llvm::failure();

  Rewriter.replaceOpWithNewOp<SumOp>(Op, Values);
  return llvm::success();
}

// The real meat and potatoes.
llvm::LogicalResult
Distribute::matchAndRewrite(mlir::Operation* Op,
                            mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();

  // Match the first Multivector operand.
  auto Itr = llvm::find_if(Op->getOpOperands(), [](mlir::OpOperand& Operand) {
      return isa<geomalg::MultivectorType>(Operand.get().getType());
    });

  if (Itr == Op->getOpOperands().end())
    return llvm::failure();

  // The multivector we want to expand and distribute over.
  mlir::OpOperand& OpOperand = *Itr;
  mlir::Value MV = OpOperand.get();
  mlir::ValueRange Blades = expandMultivector(Rewriter, MV);

  // Apply the operator to each blade summing the results.
  llvm::SmallVector<mlir::Value, 8> Results;
  for (mlir::Value Blade : Blades) {
    // Since operand values can appear multiple times,
    // IRMapping cannot be used.
    mlir::IRMapping Map;
    mlir::Operation* NewOp = Rewriter.cloneWithoutRegions(*Op, Map);
    NewOp->setOperand(OpOperand.getOperandNumber(), Blade);
    // The NewOp result type is the same as its operand or it must
    // be inferred via !geomalg.unknown.
    mlir::Value Result = NewOp->getResult(0);
    mlir::Type ResultT;
    if (NewOp->hasTrait<mlir::OpTrait::SameOperandsAndResultType>())
      ResultT = NewOp->getOperand(0).getType();
    else if (isa<geomalg::InnerProdOp>(NewOp))
      ResultT = geomalg::InnerProdOp::maybeInferType(
          NewOp->getOperand(0).getType(), NewOp->getOperand(1).getType());
    else if (isa<geomalg::NegateOp>(NewOp))
      ResultT = Blade.getType();
    else
      ResultT = geomalg::UnknownType::get(Ctx);
    Result.setType(ResultT);  // ok because we are modifying a new result.
    Results.push_back(Result);
  }

  // Replace the original op with a shiny, new SumOp.
  Rewriter.replaceOpWithNewOp<geomalg::SumOp>(Op, Results);

  return llvm::success();
}

llvm::LogicalResult
UpdateInferredTypes::matchAndRewrite(mlir::InferTypeOpInterface Op,
                            mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  if (Op->getNumResults() != 1 ||
      !isa<geomalg::UnknownType>(Op->getResult(0).getType()))
    return llvm::failure();

  // Is the inferred type still unknown?
  llvm::SmallVector<mlir::Type, 1> InferredTypes;
  auto Result = Op.inferReturnTypes(
      Op->getContext(), Op->getLoc(), Op->getOperands(),
      Op->getRawDictionaryAttrs(), Op->getPropertiesStorage(), Op->getRegions(),
      InferredTypes);

  if (InferredTypes.size() != 1 ||
      isa<geomalg::UnknownType>(InferredTypes.front()))
    return llvm::failure();

  // Just set the result type to the inferred type.
  return setResultType(Rewriter, Op.getOperation(), InferredTypes.front());
}

llvm::LogicalResult SimplifyOP::matchAndRewrite(
    geomalg::OuterProdOp OP,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Value LHS = OP.getLHS();
  mlir::Value RHS = OP.getRHS();

  auto L = dyn_cast<geomalg::BladeType>(LHS.getType());
  auto R = dyn_cast<geomalg::BladeType>(RHS.getType());
  if (!L || !R)
    return llvm::failure();

  // α ∧ B = B ∧ α = α ⌋ B
  // Outer product with a scalar can be written as
  // the inner product with the scalar on the left hand side.
  if (mlir::Value Scalar =
        L.getGrade() == 0 ? LHS :
        R.getGrade() == 0 ? RHS : mlir::Value()) {
    mlir::Value Other = Scalar == LHS ? RHS : LHS;
    if (geomalg::isUnit(Scalar)) {
      Rewriter.replaceOp(OP, Other);
    } else {
      assert(OP.getResult().getType() == Other.getType()); // Sanity check
      Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(OP, Scalar, Other);
    }
    return llvm::success();
  }

  return llvm::failure();
}

llvm::LogicalResult ZeroAbsorbToZero::matchAndRewrite(
    mlir::Operation* Op,
    mlir::PatternRewriter& Rewriter) const {
  for (mlir::Value V : Op->getOperands()) {
    if (geomalg::isZero(V))
      return replaceWithZero(Rewriter, Op);
  }

  return llvm::failure();
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
      GP, LHS, RHS);
    return llvm::success();
  }

  // a B = a ⌋ B + a ∧ B
  if (L.getGrade() == 1) {
    mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, LHS, RHS);
    mlir::Value NewWP = geomalg::OuterProdOp::create(Rewriter, Loc, LHS, RHS);
    Rewriter.replaceOpWithNewOp<geomalg::SumOp>(GP, NewLC, NewWP);
    return llvm::success();
  }

  // B^ a = -a ⌋ B + a ∧ B
  // B a = a ⌋ B - a ∧ B  (if B^ = -B)
  if (R.getGrade() == 1) {
    mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, RHS, LHS);
    mlir::Value NewWP = geomalg::OuterProdOp::create(Rewriter, Loc, RHS, LHS);
    if (L.shouldInvoNegate())
      NewWP = geomalg::NegateOp::create(Rewriter, Loc, NewWP.getType(), NewWP);
    else
      NewLC = geomalg::NegateOp::create(Rewriter, Loc, NewLC.getType(), NewLC);
    Rewriter.replaceOpWithNewOp<geomalg::SumOp>(GP, NewLC, NewWP);
    return llvm::success();
  }

  // Factor the LHS blade using a ∧ B = 1/2 (a B + B^ a).
  // With associativity we then get,
  // (a ∧ B) C = 1/2 (a (B C) + B^ (a C))
  auto [a, B] = factorBlade(Rewriter, LHS);
  mlir::Value C = RHS;
  mlir::Value B_invo;
  if (cast<geomalg::BladeType>(B.getType()).shouldInvoNegate())
    B_invo = geomalg::NegateOp::create(Rewriter, Loc, B.getType(), B);
  else
    B_invo = B;
  mlir::Value BC = geomalg::GeomProdOp::create(Rewriter, Loc, B, C);
  mlir::Value GP1 = geomalg::GeomProdOp::create(Rewriter, Loc, a, BC);
  mlir::Value aC = geomalg::GeomProdOp::create(Rewriter, Loc, a, C);
  mlir::Value GP2 = geomalg::GeomProdOp::create(Rewriter, Loc, B_invo, aC);
  mlir::Value Sum = geomalg::SumOp::create(Rewriter, Loc, GP1, GP2);

  // Use the left contraction for scalar multiplication.
  auto ScalarT = geomalg::BladeType::get(getContext(), 0);
  mlir::Value Two = geomalg::BladeOp::create(Rewriter, Loc, ScalarT, 2.0f);
  mlir::Value Half = geomalg::InverseOp::create(Rewriter, Loc, Two);
  mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, Half, Sum);
  Rewriter.replaceOp(GP, NewLC);

  return llvm::success();
}

// Expand a versor product to the geometric product.
llvm::LogicalResult ExpandVP::matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = VP.getLoc();
  mlir::ValueRange Blades = VP.getBlades();
  if (Blades.empty()) {
    Rewriter.replaceOp(VP, VP.getArg());
    return llvm::success();
  }

  auto Mult = [&](mlir::Value LHS, mlir::Value RHS) {
    return geomalg::GeomProdOp::create(Rewriter, Loc, LHS, RHS);
  };
  auto Inverse = [&](mlir::Value V) {
    return geomalg::InverseOp::create(Rewriter, Loc, V);
  };

  // Multiply left to right.
  // v₂ v₁ A v₁⁻¹ v₂⁻¹
  mlir::Value Result = Blades.back();
  for (auto Blade : llvm::reverse(Blades.drop_back()))
    Result = Mult(Result, Blade);
  Result = Mult(Result, VP.getArg());
  for (auto Blade : Blades)
    Result = Mult(Result, Inverse(Blade));

  Rewriter.replaceOp(VP, Result);

  return llvm::success();
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

  // Simplify even if we know the result type.
  if (L.getGrade() == 0) {
    // Simplify if multiplying by constant 1 as is
    // common when factoring higher dimensional blades.
    if (geomalg::isUnit(LHS)) {
      Rewriter.replaceOp(LC, RHS);
      return llvm::success();
    } else if (R.getGrade() == 0 && geomalg::isUnit(RHS)) {
      llvm_unreachable("FIXME Does this happen in practice?");
      Rewriter.replaceOp(LC, LHS);
      return llvm::success();
    }
  }

  // At this point only transform when the result type is not yet known.
  if (!isa<geomalg::UnknownType>(LC.getResult().getType()))
    return llvm::failure();

  // 3.7
  // α ⌋ B = α B
  if (L.getGrade() == 0)
    return setResultType(Rewriter, LC, R);

  // 3.8
  // B ⌋ α = 0
  if (L.getGrade() > 0 && R.getGrade() == 0)
    return replaceWithZero(Rewriter, LC);

  if (L.getGrade() == 1 && R.getGrade() == 1) {
    // 3.9
    // a ⌋ b = a ⬝ b
    // Requires a metric the application of
    // which is deferred to ApplyMetric.
    return llvm::failure();
  }

  // 3.10
  // (a ⌋ (b ∧ C) = ((a ⌋ b) ∧ C) - (b ∧ (a ⌋ C)))
  if (L.getGrade() == 1 && R.getGrade() > 1) {
    // Factor the RHS blade.
    // a ⌋ (b ∧ C) = ((a ⌋ b) ∧ C) + ((a ⌋ C) ∧ b)
    // Note we used the antisymmetric property for the second term
    mlir::Value a = LHS;
    auto [b, C] = factorBlade(Rewriter, RHS);
    // (a ⌋ b)
    mlir::Value ab = geomalg::InnerProdOp::create(Rewriter, Loc, a, b);
    // (a ⌋ C)
    mlir::Value aC = geomalg::InnerProdOp::create(Rewriter, Loc, a, C);
    // ((a ⌋ b) ∧ C)
    mlir::Value abC = geomalg::OuterProdOp::create(Rewriter, Loc, ab, C);
    // ((a ⌋ C) ∧ b)
    mlir::Value aCb = geomalg::OuterProdOp::create(Rewriter, Loc, aC, b);
    // ((a ⌋ b) ∧ C) + ((a ⌋ C) ∧ b)
    Rewriter.replaceOpWithNewOp<geomalg::SumOp>(LC, abC, aCb);

    return llvm::success();
  }

  // 3.11
  if (L.getGrade() > 1 && R.getGrade() > 0) {
    // Factor the LHS blade.
    // (a ∧ B) ⌋ C = a ⌋ (B ⌋ C)
    auto [a, B] = factorBlade(Rewriter, LHS);
    auto C = RHS;
    // (B ⌋ C)
    auto BC = geomalg::InnerProdOp::create(Rewriter, Loc, B, C);
    // (a ⌋ (B ⌋ C))
    Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(LC, a, BC);

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
  auto MV = dyn_cast<geomalg::MultivectorType>(Arg.getType());

  // The inverse of scalar 1 is just 1.
  if (BT && BT.getGrade() == 0) {
    if (geomalg::isUnit(Arg)) {
      Rewriter.replaceOp(InvOp, Arg);
      return llvm::success();
    }
  }

  // Support only k-blades.
  // Scalars are lowered to division in the dialect conversion.
  // Arbitrary multivectors are unsupported by this pass.
  if ((!BT && !MV) ||
      (BT && BT.getGrade() == 0) ||
      (MV && !MV.isBlade(1)))
    return llvm::failure();


  // For basis blades we can simplify to a Negate since we know the grade.
  mlir::Value Reverse;
  if (BT) {
    Reverse = BT.shouldReverseNegate()
      ? geomalg::NegateOp::create(Rewriter, Loc, BT, Arg)
      : Arg;
  } else {
    Reverse = geomalg::ReverseOp::create(Rewriter, Loc, Arg);
  }

  mlir::Value Squared = geomalg::InnerProdOp
    ::create(Rewriter, Loc, Arg, Arg);
  mlir::Value SquareInverse = geomalg::InverseOp
    ::create(Rewriter, Loc, Squared);

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
    Rewriter.replaceOpWithNewOp<geomalg::NegateOp>(RevOp, BT, RevOp.getArg());
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
  if (!L || !R || L.getGrade() != 1 || R.getGrade() != 1)
    return llvm::failure();

  // DotResult ∈ {-1, 0, 1}
  int DotResult = Metric.dotProduct(L.getBladeTag(), R.getBladeTag());

  if (DotResult == 0)
    return replaceWithZero(Rewriter, LC);

  // Cast both operands to scalar and negate if the result was -1.
  mlir::Type ScalarT = geomalg::BladeType::get(getContext(), 0);
  mlir::Value CastL = geomalg::CastOp::create(Rewriter, Loc, ScalarT, LHS);
  mlir::Value CastR = geomalg::CastOp::create(Rewriter, Loc, ScalarT, RHS);
  if (DotResult == -1) {
    mlir::Value Result = geomalg::InnerProdOp::create(
        Rewriter, Loc, CastL, CastR);
    Rewriter.replaceOpWithNewOp<geomalg::NegateOp>(LC, ScalarT, Result);
  } else {
    assert(DotResult == 1);
    Rewriter.replaceOpWithNewOp<geomalg::InnerProdOp>(LC, CastL, CastR);
  }
  return llvm::success();
}

llvm::LogicalResult ExpandPass::initialize(mlir::MLIRContext* Ctx) {
  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<ExpandVP,
         ExpandSum,
         Distribute,
         ZeroAbsorbToZero,
         ExpandLC,
         ExpandGP,
         SimplifyOP,
         ExpandInverse,
         ExpandReverse,
         UpdateInferredTypes>(Ctx);
  if (metric != geomalg::MetricKind::unknown)
    PS.add<ApplyMetric>(Ctx, geomalg::Metric::get(metric));
  geomalg::transforms::populateGeneratedPDLLPatterns(PS);
  Patterns = mlir::FrozenRewritePatternSet(std::move(PS),
                        disabledPatterns,
                        enabledPatterns);
  return llvm::success();
}

void ExpandPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, Patterns)))
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
