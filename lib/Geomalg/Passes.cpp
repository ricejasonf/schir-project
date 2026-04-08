#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/Dominance.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <cassert>
#include <string>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_EXPANDPASS
#define GEN_PASS_DEF_SIMPLIFYPASS
#define GEN_PASS_DEF_MATRIXPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace geomalg::simplify {
#include "geomalg/Transforms/Simplify.h.inc"
}

namespace geomalg {
// Because lldb cannot look at mlir::Operation, ...
void dumpOp(mlir::Operation* Op) {
  Op->dump();
}

// Because lldb cannot look at mlir::Operation, ...
void dumpBlock(mlir::Operation* Op) {
  Op->getBlock()->dump();
}

void dumpBlock(mlir::Value V) {
  V.getParentBlock()->dump();
}
}

using namespace geomalg;

// Expand a multivector for the purpose of expanding
// via distribution or unnesting of sums.
static mlir::ValueRange
expandMultivector(mlir::PatternRewriter& Rewriter, mlir::Value MV) {
  assert(isa<geomalg::MultivectorLike>(MV.getType()) &&
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
    auto MVT = llvm::cast<geomalg::MultivectorLike>(MV.getType());
    llvm::SmallVector<mlir::Type, 8> BladeTypes(MVT.getBlades());
    ExpandOp = geomalg::ExpandOp::create(Rewriter, MV.getLoc(),
                                         BladeTypes, MV);
  }
  return ExpandOp.getResults();
}

static mlir::Type inferReturnType(mlir::InferTypeOpInterface Op) {
  assert(!Op->hasTrait<mlir::OpTrait::VariadicResults>() &&
      "expecting operation with single result");
  llvm::SmallVector<mlir::Type, 1> InferredTypes;
  auto Result = Op.inferReturnTypes(
      Op->getContext(), Op->getLoc(), Op->getOperands(),
      Op->getRawDictionaryAttrs(), Op->getPropertiesStorage(), Op->getRegions(),
      InferredTypes);
  assert(InferredTypes.front());
  return InferredTypes.front();
}

void replaceOp(mlir::RewriterBase& RB, mlir::Operation* Op,
               mlir::ValueRange Vs) {
  assert(InferredResultBase::isCompatibleReturnTypes(
                                    Vs, Op->getResultTypes()) &&
      "replacement value(s) must have valid type narrowings");
  RB.replaceOp(Op, Vs);
}

template <typename OpTy, typename ...Args>
static OpTy replaceOpWithNewOp(mlir::RewriterBase& RB,
                               mlir::Operation* Op, Args&& ...args) {
  auto NewOp = OpTy::create(RB, Op->getLoc(),
                            std::forward<Args>(args)...);

  if (auto S = dyn_cast<SumOp>(Op)) {
    if constexpr(std::is_same_v<OpTy, SumOp>)
      assert(isUnknown(S.getResult().getType()) || NewOp.getArgs().size() <= S.getArgs().size());
  }
  replaceOp(RB, Op, NewOp->getResults());
  return NewOp;
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

class SimplifyPass : public geomalg::impl::SimplifyPassBase<SimplifyPass> {
  using Base = geomalg::impl::SimplifyPassBase<SimplifyPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;
  void runOnOperation() override;
  llvm::LogicalResult initialize(mlir::MLIRContext*) override;
};

class MatrixPass : public geomalg::impl::MatrixPassBase<MatrixPass> {
  using Base = geomalg::impl::MatrixPassBase<MatrixPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;
  void runOnOperation() override;
  llvm::LogicalResult initialize(mlir::MLIRContext*) override;
};

// Rewriters for expansion.

struct ExpandConvert : mlir::OpRewritePattern<geomalg::ConvertOp> {
  using Base = mlir::OpRewritePattern<geomalg::ConvertOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandConvert");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::ConvertOp Op, mlir::PatternRewriter& Rewriter) const override;
};

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

// Convert a linear operation into matrix multiplication.
struct ExpandMatmul : mlir::OpTraitRewritePattern<geomalg::Linear> {
  using Base = mlir::OpTraitRewritePattern<geomalg::Linear>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    setDebugName("ExpandMatmul");
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

struct RemoveCast : mlir::OpRewritePattern<geomalg::CastOp> {
  using Base = mlir::OpRewritePattern<geomalg::CastOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("RemoveCast");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::CastOp, mlir::PatternRewriter& Rewriter) const override;
};

struct RemoveExpand : mlir::OpRewritePattern<geomalg::ExpandOp> {
  using Base = mlir::OpRewritePattern<geomalg::ExpandOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("RemoveExpand");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::ExpandOp, mlir::PatternRewriter&) const override;
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

struct DistributeVP : mlir::OpRewritePattern<geomalg::VersorProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::VersorProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("DistributeVP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const override;
};

struct ExpandVP : mlir::OpRewritePattern<geomalg::VersorProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::VersorProdOp>;

  geomalg::Metric Metric;

  template <typename ...Args>
  ExpandVP(geomalg::Metric M, Args&& ...args)
    : Base(std::forward<Args>(args)...)
    , Metric(M)
  { }

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

struct SimplifyNegate : mlir::OpRewritePattern<geomalg::NegateOp> {
  using Base = mlir::OpRewritePattern<geomalg::NegateOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyNegate");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::NegateOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifyInverse : mlir::OpRewritePattern<geomalg::InverseOp> {
  using Base = mlir::OpRewritePattern<geomalg::InverseOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyInverse");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::InverseOp InvOp,
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

struct ExpandGradeInvo : mlir::OpRewritePattern<geomalg::GradeInvoOp> {
  using Base = mlir::OpRewritePattern<geomalg::GradeInvoOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandGradeInvo");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::GradeInvoOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand inner product of basis 1-blades.
// If the metric is unspecified (unknown) this does nothing.
struct ApplyMetric : mlir::OpRewritePattern<geomalg::InnerProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::InnerProdOp>;

  geomalg::Metric Metric;

  template <typename ...Args>
  ApplyMetric(geomalg::Metric M, Args&& ...args)
    : Base(std::forward<Args>(args)...)
    , Metric(M)
  { }

  void initialize() {
    setDebugName("ApplyMetric");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::InnerProdOp LC,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifyMul : mlir::OpTraitRewritePattern<geomalg::IsMul> {
  using Base = mlir::OpTraitRewritePattern<geomalg::IsMul>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    setDebugName("SimplifyMul");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifySum : mlir::OpRewritePattern<geomalg::SumOp> {
  using Base = mlir::OpRewritePattern<geomalg::SumOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyMul");
  }

  llvm::LogicalResult matchAndRewrite(
      SumOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifyDot : mlir::OpRewritePattern<geomalg::DotOp> {
  using Base = mlir::OpRewritePattern<geomalg::DotOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyDot");
  }

  llvm::LogicalResult matchAndRewrite(
      DotOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Partial ordering where all non-blades are less than
// any blade.
auto compareBlades(mlir::Value Aval, mlir::Value Bval) -> bool {
  geomalg::BladeType A = dyn_cast<BladeType>(Aval.getType());
  geomalg::BladeType B = dyn_cast<BladeType>(Bval.getType());
  if (!A || !B)
    return !A && B;
  return compareBladeTypes(A, B);
}

// For sorting operands within a block.
auto compareBlockValues(mlir::Value A, mlir::Value B) -> bool {
  // Return true if A < B.
  if (auto BA = dyn_cast<mlir::BlockArgument>(A)) {
    if (auto BB = dyn_cast<mlir::BlockArgument>(B))
      return BA.getArgNumber() < BB.getArgNumber();
    else
      return true; // All block args precede opresults.
  }
  auto OA = cast<mlir::OpResult>(A);
  if (auto OB = dyn_cast<mlir::OpResult>(B)) {
    if (OA.getDefiningOp() == OB.getDefiningOp())
      return OA.getResultNumber() < OB.getResultNumber();
    else if (OA.getParentBlock() == OB.getParentBlock())
      return OA.getOwner()->isBeforeInBlock(OB.getOwner());
    else
      return OA.getParentRegion()->isProperAncestor(OB.getParentRegion());
  }
  // B is a BlockArgument which precedes OpResult A.
  return false;
}
}  // namespace

static llvm::LogicalResult
setResultType(mlir::PatternRewriter& Rewriter,
              mlir::Operation* Op, mlir::Type Type) {
  mlir::Value Result = Op->getResult(0);
  assert(Result.getType() != Type);
  assert(isValidNarrowing(Result.getType(), Type));
  Rewriter.startOpModification(Op);
  Result.setType(Type);
  Rewriter.finalizeOpModification(Op);
  // Notify the uses.
  Rewriter.replaceAllUsesWith(Result, Result);
  return llvm::success();
}

static llvm::LogicalResult
replaceWithZero(mlir::PatternRewriter& Rewriter,
                mlir::Operation* Op) {
  auto ZeroT = geomalg::ZeroType::get(Op->getContext());
  replaceOpWithNewOp<geomalg::BladeOp>(Rewriter, Op, ZeroT, 0);
  return llvm::success();
}

// Create outer product and canonicalize the order of
// operands negating the result the operands are swapped.
// The result might not be directly of the OuterProdOp.
static mlir::Value
createOuterProd(mlir::PatternRewriter& Rewriter,
                mlir::Location Loc,
                mlir::Value LHS, mlir::Value RHS) {
  // Use InferredResult for OuterProdOp
  mlir::Operation* Op = geomalg::OuterProdOp::create(Rewriter, Loc,
                                                     LHS, RHS);
  mlir::Value Result = Op->getResult(0);
  if (auto BT = dyn_cast<geomalg::BladeType>(Result.getType())) {
    if (!BT.isCanonical())
      Op = OSwapOp::create(Rewriter, Loc, Result);
  }
  return Op->getResult(0);
}

// Peel of a basis vector from a basis blade.
static std::pair<mlir::Value, mlir::Value>
factorBlade(mlir::PatternRewriter& Rewriter, mlir::Value V) {
  mlir::Location Loc = V.getLoc();
  auto BT = cast<geomalg::BladeType>(V.getType());
  if (!BT.isCanonical()) {
    BT = BT.getCanonicalType();
    V = OSwapOp::create(Rewriter, Loc, V);
    assert(V.getType() == mlir::Type(BT));
  }
  assert(BT.getGrade() > 1 && "prevent idempotent rewrite");
  auto [Type_a, Type_B] = BT.factor();
  mlir::Value a = geomalg::BladeOp::create(Rewriter, Loc, Type_a, 1.0);
  mlir::Value B = geomalg::CastOp::create(Rewriter, Loc, Type_B, V);
  return {a, B};
}

static bool requiresMetric(mlir::Operation* Op) {
  if (auto IP = dyn_cast<InnerProdOp>(Op)) {
    auto L = dyn_cast<BladeType>(IP.getLHS().getType());
    auto R = dyn_cast<BladeType>(IP.getRHS().getType());
    if (L && R && L.getGrade() > 0 && R.getGrade() > 0)
      return true;
  }
  return false;
}

llvm::LogicalResult
ExpandConvert::matchAndRewrite(geomalg::ConvertOp Op,
                               mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = Op.getLoc();
  mlir::Value Arg = Op.getArg();
  mlir::Value Result = Op.getResult();
  mlir::Type ArgT = Arg.getType();
  mlir::Type ResultT = Result.getType();

  if (ArgT == ResultT) {
    replaceOp(Rewriter, Op, Arg);
    return llvm::success();
  }

  // Vector to UnitVector
  if (auto MV = dyn_cast<MultivectorLike>(ArgT);
      MV && MV.isVector() && isa<UnitVectorType>(ResultT)) {
    // Copy pasted from ExpandInverse.
    mlir::Value Squared = geomalg::InnerProdOp
      ::create(Rewriter, Loc, Arg, Arg);
    mlir::Value SquareInverse = geomalg::InverseOp
      ::create(Rewriter, Loc, Squared);

    // Multiply the inverse of norm squared and the Arg vector.
    mlir::Value IP = InnerProdOp::create(Rewriter, Loc, SquareInverse, Arg);
    replaceOpWithNewOp<CastOp>(Rewriter, Op, ResultT, IP);
    return llvm::success();
  }

  Op.emitError("no known conversion");
  return llvm::failure();
}

llvm::LogicalResult
ExpandSum::matchAndRewrite(geomalg::SumOp Op,
                           mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = Op.getLoc();

  if (Op.getArgs().size() == 0)
    return replaceWithZero(Rewriter, Op);

  // Sum of single operand can be elided safely.
  if (Op.getArgs().size() == 1) {
    replaceOp(Rewriter, Op, Op.getArgs().front());
    return llvm::success();
  }

  // Collect Values unnesting (directly nested) sums discarding Zeros.
  llvm::SmallVector<mlir::Value, 8> Values;
  mlir::Type ResultT = Op.getResult().getType();
  for (mlir::Value V : Op.getOperands()) {
    if (auto BT = dyn_cast<BladeType>(V.getType())) {
      // Fold sums of a common blade.
      if (auto S = V.getDefiningOp<SumOp>(); S && BT == ResultT)
        llvm::append_range(Values, S.getArgs());
      else
        Values.push_back(V);
    } else if (isa<MultivectorLike>(V.getType())) {
      llvm::append_range(Values, expandMultivector(Rewriter, V));
    } else if (isa<UnknownType>(V.getType())) {
      return llvm::failure();
    } else {
      // Discard zeros.
      assert(isa<ZeroType>(V.getType()));
    }
  }

  // Sort by BladeTag putting all non-blades at the front.
  llvm::stable_sort(Values, compareBlades);
  if (llvm::equal(Values, Op.getOperands()))
    return llvm::failure();

  // Group by type putting all blade types in their own sum
  // so that all operands are either blades or unknown.
  using ValuesTy = llvm::SmallVector<mlir::Value, 8>;
  llvm::SmallVector<std::pair<mlir::Type, ValuesTy>> TypeMap;
  for (mlir::Value V : Values) {
    auto HasTheType = [&](auto TypeMapNode) {
      auto const& [T, _] = TypeMapNode;
      return T == V.getType();
    };
    auto Itr = llvm::find_if(TypeMap, HasTheType);
    if (Itr == TypeMap.end())
      TypeMap.push_back({V.getType(), {V}});
    else
      Itr->second.push_back(V);
  }

  // Values should remain in sorted order.
  llvm::SmallVector<mlir::Value, 8> GroupedValues;
  for (auto& TypeMapNode : TypeMap) {
    auto const& [T, Vs] = TypeMapNode;
    if (isa<BladeType>(T)) {
      if (Vs.size() == 1) {
        GroupedValues.push_back(Vs.front());
      } else if (Vs.size() > 1) {
        mlir::Value S = SumOp::create(Rewriter, Loc, Vs);
        GroupedValues.push_back(S);
      }
      // Do not make an empty sum. (ie its like zero)
    } else if (isZero(T)) {
      // Discard (nested) zeros.
    } else {
      assert(isUnknown(T));
      llvm::append_range(GroupedValues, Vs);
    }
  }

  llvm::stable_sort(GroupedValues, compareBlades);
  replaceOpWithNewOp<SumOp>(Rewriter, Op, GroupedValues);
  return llvm::success();
}

// The real meat and potatoes.
llvm::LogicalResult
Distribute::matchAndRewrite(mlir::Operation* Op,
                            mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();

  // Match the first Multivector operand.
  auto Itr = llvm::find_if(Op->getOpOperands(), [](mlir::OpOperand& Operand) {
      return isa<geomalg::MultivectorLike>(Operand.get().getType());
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
    // Keep result type valid if it is inferred.
    if (auto InferOp = dyn_cast<mlir::InferTypeOpInterface>(NewOp)) {
      mlir::Type ResultT = inferReturnType(InferOp);
      Result.setType(ResultT);  // ok because we are modifying
                                // a new (cloned) result.
    }
    Results.push_back(Result);
  }

  // Replace the original op with a shiny, new SumOp.
  bool IsUnit = isa<UnitVectorType>(Op->getResult(0).getType());
  replaceOpWithNewOp<geomalg::SumOp>(Rewriter, Op, IsUnit, Results);

  return llvm::success();
}

llvm::LogicalResult
ExpandMatmul::matchAndRewrite(mlir::Operation* Op,
                              mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  mlir::Location Loc = Op->getLoc();

  if (isa<MatmulOp>(Op))
    return llvm::failure();

  // Prevent nesting
  if (Op->getParentOfType<MatmulOp>())
    return llvm::failure();

  assert(Op->getResults().size() == 1);
  if (!isa<MultivectorLike, UnknownType>(Op->getResult(0).getType()))
    return llvm::failure();

  // Match the first Multivector operand.
  auto Itr = llvm::find_if(Op->getOpOperands(), [](mlir::OpOperand& Operand) {
      return isa<geomalg::MultivectorLike>(Operand.get().getType());
    });

  if (Itr == Op->getOpOperands().end())
    return llvm::failure();

  mlir::OpOperand& OpOperand = *Itr;
  unsigned MVOperandIndex = OpOperand.getOperandNumber();
  mlir::Value MV = OpOperand.get();
  auto MVL = cast<MultivectorLike>(MV.getType());
  llvm::ArrayRef<BladeType> Blades = MVL.getBlades();
  auto MM = MatmulOp::create(Rewriter, Loc, MV,
                             /*NumRegions=*/Blades.size());

  // Instantiate the regions of the new MatmulOp.
  for (auto&& [Region, BladeT] : llvm::zip(MM.getBodies(), Blades)) {
    // Instantiate body substituting the multivector
    // with the basis element for this blade.
    mlir::OpBuilder::InsertionGuard IG(Rewriter);
    mlir::Block* Block = Rewriter.createBlock(&Region);
    mlir::OpBuilder Builder = mlir::OpBuilder(MM);
    mlir::Value BasisElement = BladeOp::create(Builder, Loc, BladeT, 1.0f);
    // Only substitute the one operand.
    mlir::IRMapping IRMap;
    mlir::Operation* NewOp = Rewriter.clone(*Op, IRMap);
    NewOp->setOperand(MVOperandIndex, BasisElement);
    ReturnOp::create(Rewriter, Loc, NewOp->getResult(0));
  }

  // Update the result type after adding the regions.
  mlir::Value Result = MM.getResult();
  Result.setType(inferReturnType(MM));

  replaceOp(Rewriter, Op, Result);
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
  mlir::Type ResultT = inferReturnType(Op);
  if (isa<geomalg::UnknownType>(ResultT))
    return llvm::failure();

  // Just set the result type to the inferred type.
  return setResultType(Rewriter, Op.getOperation(), ResultT);
}

llvm::LogicalResult RemoveCast::matchAndRewrite(
    geomalg::CastOp CastOp,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Type ArgT = CastOp.getArg().getType();
  mlir::Type ResultT = CastOp.getResult().getType();

  if (ArgT != ResultT)
    return llvm::failure();

  replaceOp(Rewriter, CastOp, CastOp.getArg());
  return llvm::success();
}

llvm::LogicalResult RemoveExpand::matchAndRewrite(
    geomalg::ExpandOp ExpandOp,
    mlir::PatternRewriter& Rewriter) const {
  if (isa<UnknownType>(ExpandOp.getArg().getType()))
    return llvm::failure();

  // Replace unnecessary ExpandOps.
  if (auto SOp = ExpandOp.getArg().getDefiningOp<SumOp>()) {
    replaceOp(Rewriter, ExpandOp, SOp.getOperands());
    return llvm::success();
  }

  if (isa<MultivectorLike>(ExpandOp.getArg().getType()))
    return llvm::failure();

  // Replace unary expands with their operand.
  assert(ExpandOp.getResults().size() == 1);
  replaceOp(Rewriter, ExpandOp, ExpandOp.getArg());
  return llvm::success();
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
    replaceOpWithNewOp<geomalg::InnerProdOp>(
      Rewriter, GP, LHS, RHS);
    return llvm::success();
  }

  // B α = α ⌋ B
  if (R.getGrade() == 0) {
    replaceOpWithNewOp<geomalg::InnerProdOp>(
      Rewriter, GP, RHS, LHS);
    return llvm::success();
  }

  // a B = a ⌋ B + a ∧ B
  if (L.getGrade() == 1) {
    mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, LHS, RHS);
    mlir::Value NewWP = createOuterProd(Rewriter, Loc, LHS, RHS);
    replaceOpWithNewOp<geomalg::SumOp>(Rewriter, GP, NewLC, NewWP);
    return llvm::success();
  }

  // B^ a = -a ⌋ B + a ∧ B
  // B a = a ⌋ B - a ∧ B  (if B^ = -B)
  if (R.getGrade() == 1) {
    mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, RHS, LHS);
    mlir::Value NewWP = createOuterProd(Rewriter, Loc, RHS, LHS);
    if (L.shouldInvoNegate())
      NewWP = geomalg::NegateOp::create(Rewriter, Loc, NewWP.getType(), NewWP);
    else
      NewLC = geomalg::NegateOp::create(Rewriter, Loc, NewLC.getType(), NewLC);
    replaceOpWithNewOp<geomalg::SumOp>(Rewriter, GP, NewLC, NewWP);
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
  mlir::Value Half = geomalg::BladeOp::create(Rewriter, Loc, ScalarT, 0.5f);
  mlir::Value NewLC = geomalg::InnerProdOp::create(Rewriter, Loc, Half, Sum);
  replaceOp(Rewriter, GP, NewLC);

  return llvm::success();
}

// Distribute the versor product of the first blade
// across the argument if its a multivector.
llvm::LogicalResult DistributeVP::matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = VP.getLoc();
  mlir::ValueRange Versors = VP.getVersors();
  if (Versors.empty()) {
    replaceOp(Rewriter, VP, VP.getArg());
    return llvm::success();
  }

  mlir::Value Arg = VP.getArg();
  auto MV = dyn_cast<MultivectorLike>((Arg.getType()));
  if (!MV)
    return llvm::failure();

  llvm::SmallVector<mlir::Value, 8> SumArgs;
  mlir::ValueRange Values = expandMultivector(Rewriter, Arg);
  for (mlir::Value V : Values) {
    mlir::Value SumArg = VersorProdOp::create(Rewriter, Loc, V, Versors);
    SumArgs.push_back(SumArg);
  }
  replaceOpWithNewOp<SumOp>(Rewriter, VP, SumArgs);

  return llvm::success();
}

// Expand a versor product to the geometric product.
llvm::LogicalResult ExpandVP::matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const {
  mlir::Value Arg = VP.getArg();
  mlir::ValueRange Versors = VP.getVersors();

  BladeType BT = dyn_cast<BladeType>(Arg.getType());
  if (!BT)
    return llvm::failure();

  if (Versors.empty()) {
    replaceOp(Rewriter, VP, VP.getArg());
    return llvm::success();
  }

  mlir::Location Loc = VP.getLoc();
  auto Mult = [&](mlir::Value LHS, mlir::Value RHS) {
    return GeomProdOp::create(Rewriter, Loc, LHS, RHS);
  };
  auto Inverse = [&](mlir::Value V) {
    if (isUnitVector(V))
      return V;
    return mlir::Value(InverseOp::create(Rewriter, Loc, V));
  };
  auto Negate = [&](mlir::Value V) {
    return mlir::Value(NegateOp::create(Rewriter, Loc, V));
  };
  auto GradeInvo = [&](mlir::Value V) {
    return mlir::Value(GradeInvoOp::create(Rewriter, Loc, V));
  };

  mlir::Value Versor = Versors.front();
  BladeType SingleVBT = dyn_cast<BladeType>(Versor.getType());
  llvm::ArrayRef<BladeType> VersorBladeTypes;
  if (auto MVT = dyn_cast<MultivectorLike>(Versor.getType()))
    VersorBladeTypes = MVT.getBlades();
  else if (SingleVBT)
    VersorBladeTypes = SingleVBT;
  else
    llvm_unreachable("expecting a valid type for versor");

  // Check if BT is known to be orthogonal to all the elements
  // of Versor to bypass some distributing.
  bool IsOrthogonal = bool(Metric);
  if (Metric) {
    for (BladeType VBT : VersorBladeTypes) {
      if (!Metric.isOrthogonal(BT.getBladeTag(), VBT.getBladeTag())) {
        IsOrthogonal = false;
        break;
      }
    }
  }

  // v₂ (v₁ A v₁⁻¹) v₂⁻¹
  // Expand the application of one versor at a time
  // to tame the combinational explosions which requires
  // this pattern to have a relatively low "benefit".

  // Leftmost and rightmost operands
  // v₁ A v₁⁻¹
  mlir::Value VL = Negate(GradeInvo(Versor));
  mlir::Value VR = Inverse(Versor);

  mlir::Value Result;
  if (IsOrthogonal) {
    // Use the antisymmetric property.
    mlir::Value Dot = InnerProdOp::create(Rewriter, Loc, VL, VR);
    Result = InnerProdOp::create(Rewriter, Loc, Dot, Arg); // scalar mult
    Result = NegateOp::create(Rewriter, Loc, Result.getType(), Result);
  } else {
    Result = Mult(Versor, Mult(Arg, Inverse(Versor)));
  }

  // Recreate VP for remaining versors.
  mlir::ValueRange RestVersors = VP.getVersors().drop_front();
  if (!RestVersors.empty())
    Result = VersorProdOp::create(Rewriter, Loc, Result, RestVersors);

  replaceOp(Rewriter, VP, Result);
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
  {
    // Handle unit vector dot product with itself.
    auto L = dyn_cast<geomalg::UnitVectorType>(LHS.getType());
    auto R = dyn_cast<geomalg::UnitVectorType>(RHS.getType());
    if (L && R && L == R) {
      mlir::Type ScalarT = geomalg::BladeType::get(getContext(), 0);
      replaceOpWithNewOp<BladeOp>(Rewriter, LC, ScalarT, 1.0f);
      return llvm::success();
    }
  }

  auto L = dyn_cast<geomalg::BladeType>(LHS.getType());
  auto R = dyn_cast<geomalg::BladeType>(RHS.getType());
  if (!L || !R)
    return llvm::failure();

  // Simplify even if we know the result type.
  if (L.getGrade() == 0) {
    // Simplify if multiplying by constant 1 as is
    // common when factoring higher dimensional blades.
    if (geomalg::isUnitBlade(LHS)) {
      replaceOp(Rewriter, LC, RHS);
      return llvm::success();
    } else if (R.getGrade() == 0 && geomalg::isUnitBlade(RHS)) {
      llvm_unreachable("FIXME Does this happen in practice?");
      replaceOp(Rewriter, LC, LHS);
      return llvm::success();
    }
  }

  // At this point only transform when the result type is not yet known.
  if (!isa<geomalg::UnknownType>(LC.getResult().getType()))
    return llvm::failure();

  // 3.7 and 3.8 are handled by type inference.
  // 3.7
  // α ⌋ B = α B
  // 3.8
  // B ⌋ α = 0

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
    mlir::Value a = LHS;
    auto [b, C] = factorBlade(Rewriter, RHS);
    // (a ⌋ b)
    mlir::Value ab = geomalg::InnerProdOp::create(Rewriter, Loc, a, b);
    // (a ⌋ C)
    mlir::Value aC = geomalg::InnerProdOp::create(Rewriter, Loc, a, C);
    // ((a ⌋ b) ∧ C)
    mlir::Value abC = createOuterProd(Rewriter, Loc, ab, C);
    // - (b ∧ (a ⌋ C))
    mlir::Value baC = createOuterProd(Rewriter, Loc, b, aC);
    baC = NegateOp::create(Rewriter, Loc, baC);
    // ((a ⌋ b) ∧ C) - (b ∧ (a ⌋ C))
    replaceOpWithNewOp<geomalg::SumOp>(Rewriter, LC, abC, baC);
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
    replaceOpWithNewOp<geomalg::InnerProdOp>(Rewriter, LC, a, BC);
    return llvm::success();
  }

  return llvm::failure();
}

llvm::LogicalResult SimplifyNegate::matchAndRewrite(
    NegateOp Op,
    mlir::PatternRewriter& Rewriter) const {
  // Negate of negate cancels.
  if (auto NOp = Op.getArg().getDefiningOp<NegateOp>()) {
    replaceOp(Rewriter, Op, NOp.getArg());
    return llvm::success();
  }

  return llvm::failure();
}

llvm::LogicalResult SimplifyInverse::matchAndRewrite(
    geomalg::InverseOp InvOp,
    mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  mlir::Location Loc = InvOp.getLoc();
  mlir::Value Arg = InvOp.getArg();

  // Inverse of a unit vector is always itself.
  if (auto UV = dyn_cast<UnitVectorType>(Arg.getType())) {
    replaceOp(Rewriter, InvOp, Arg);
    return llvm::success();
  }

  // The inverse of scalar 1 is just 1.
  if (auto BT = dyn_cast<geomalg::BladeType>(Arg.getType());
      BT && BT.getGrade() == 0) {
    if (geomalg::isUnitBlade(Arg)) {
      replaceOp(Rewriter, InvOp, Arg);
      return llvm::success();
    }
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
  auto MV = dyn_cast<geomalg::MultivectorLike>(Arg.getType());

  // Support only k-blades.
  // Scalars are lowered to division in the dialect conversion.
  // Arbitrary multivectors are unsupported by this pass.
  if ((!BT && !MV) ||
      (BT && BT.getGrade() == 0) ||
      (MV && !MV.isVector()))
    return llvm::failure();


  // For basis blades we can simplify to a Negate since we know the grade.
  mlir::Value Reverse;
  if (BT) {
    Reverse = BT.shouldReverseNegate()
      ? geomalg::NegateOp::create(Rewriter, Loc, Arg.getType(), Arg)
      : Arg;
  } else {
    Reverse = geomalg::ReverseOp::create(Rewriter, Loc, Arg);
  }

  mlir::Value Squared = geomalg::InnerProdOp
    ::create(Rewriter, Loc, Arg, Arg);
  mlir::Value SquareInverse = geomalg::InverseOp
    ::create(Rewriter, Loc, Squared);

  // Multiply the inverse of norm squared and reverse blade.
  replaceOpWithNewOp<geomalg::InnerProdOp>(
      Rewriter, InvOp, SquareInverse, Reverse);
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
    replaceOpWithNewOp<geomalg::NegateOp>(Rewriter, RevOp, Arg.getType(), Arg);
  else
    replaceOp(Rewriter, RevOp, Arg);

  return llvm::success();
}

llvm::LogicalResult ExpandGradeInvo::matchAndRewrite(
    geomalg::GradeInvoOp InvoOp,
    mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  mlir::Location Loc = InvoOp.getLoc();
  mlir::Value Arg = InvoOp.getArg();

  auto BT = dyn_cast<geomalg::BladeType>(Arg.getType());

  // Multivectors are handled by the Distribute rewriter.
  if (!BT)
    return llvm::failure();

  if (BT.shouldInvoNegate())
    replaceOpWithNewOp<geomalg::NegateOp>(Rewriter, InvoOp, Arg.getType(), Arg);
  else
    replaceOp(Rewriter, InvoOp, Arg);

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

  {
    // Check for vectors with only orthogonal basis vectors.
    auto MVL = dyn_cast<MultivectorLike>(LHS.getType());
    auto MVR = dyn_cast<MultivectorLike>(RHS.getType());
    if (MVL && MVR) {
      bool IsOrthoBasis = true;
      for (BladeType L : MVL.getBlades())
        for (BladeType R : MVR.getBlades())
          if (L != R && !Metric.isOrthogonal(L.getBladeTag(), R.getBladeTag()))
            IsOrthoBasis = false;
      if (IsOrthoBasis) {
        mlir::Type ScalarT = geomalg::BladeType::get(getContext(), 0);
        replaceOpWithNewOp<DotOp>(Rewriter, LC, ScalarT, LHS, RHS);
        return llvm::success();
      }
    }
  }

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
    replaceOpWithNewOp<geomalg::NegateOp>(Rewriter, LC,
                                  Result.getType(), Result);
  } else {
    assert(DotResult == 1);
    replaceOpWithNewOp<geomalg::InnerProdOp>(Rewriter, LC, CastL, CastR);
  }
  return llvm::success();
}

template <typename T>
auto GetDefiningOp = [](mlir::Value V) -> T {
  return V.getDefiningOp<T>();
};

llvm::LogicalResult SimplifyMul::matchAndRewrite(
    mlir::Operation* Op,
    mlir::PatternRewriter& Rewriter) const {
  if (requiresMetric(Op) || isUnknown(Op->getResult(0).getType())) {
    //Op->emitError("cannot simplify inner product without metric");
    // Do not emit a gazillion op errors.
    //std::exit(1);
    return llvm::failure();
  }

  mlir::ValueRange Operands = Op->getOperands();
  auto GetDefiningMul = [](mlir::Value V) -> mlir::Operation* {
    if (V.getDefiningOp() &&
        V.getDefiningOp()->hasTrait<geomalg::IsMul>())
      return V.getDefiningOp();
    return nullptr;
  };

  if (isa<geomalg::CMulOp>(Op) &&
      !llvm::any_of(Operands, GetDefiningMul) &&
      !llvm::any_of(Operands, GetDefiningOp<geomalg::NegateOp>) &&
      !llvm::any_of(Operands, GetDefiningOp<geomalg::CastOp>) &&
      llvm::count_if(Operands, GetDefiningOp<geomalg::BladeOp>) <= 1 &&
      (llvm::none_of(Operands, isUnitBlade)))
    return llvm::failure();

  // Collect operands.
  llvm::SmallVector<mlir::Value, 8> NewOperands;
  for (mlir::Value V : Operands) {
    if (mlir::Operation* MOp = GetDefiningMul(V)) {
      if (requiresMetric(MOp)) {
        //MOp->emitError("cannot simplify inner product without metric");
        // Do not emit a gazillion op errors.
        //std::exit(1);
        return llvm::failure();
      }
      llvm::append_range(NewOperands, MOp->getOperands());
    } else {
      NewOperands.push_back(V);
    }
  }

  // Fold all results of NegateOp, OSwapOp, and BladeOp.
  unsigned NegateCount = 0;
  float Accum = 1.0f;
  for (mlir::Value& V : NewOperands) {
    if (auto NOp = V.getDefiningOp<geomalg::NegateOp>()) {
      ++NegateCount;
      // Remove the use of the negate.
      V = NOp.getArg();
    } else if (auto SwapOp = V.getDefiningOp<geomalg::OSwapOp>()) {
      ++NegateCount;
      // Remove the use of the oswap.
      V = SwapOp.getArg();
    } else if (auto BOp = V.getDefiningOp<geomalg::BladeOp>()) {
      // Since float powers of 2 are closed under multiplication
      // and represented exactly, they are associative and commutative.
      float C = BOp.getFloat();
      int n = 0;
      float m = std::frexp(C, &n);
      float m_abs = std::abs(m);
      // Powers of 2 are part of the algebra and cancel each other out.
      // (We do not support arbitrary constants at yet.)
      assert((C == 0 || m_abs == 0.5f)
          && "we are only expecting powers of 2 right now");

      Accum *= std::abs(C);
      if (C < 0.0f)
        ++NegateCount;
      // Mark to remove from list.
      V = mlir::Value();
    } else if (auto COp = V.getDefiningOp<geomalg::CastOp>()) {
      V = COp.getArg();
    }
  }

  // Remove nulls.
  llvm::erase(NewOperands, mlir::Value());

  mlir::Location Loc = Op->getLoc();
  // Create and push new BladeOp if it is not the id element
  // or it would be the only operand.
  bool IsSingleBlade = NewOperands.empty();
  if (Accum != 1.0f || NewOperands.empty()) {
    mlir::Type ScalarT = geomalg::BladeType::get(getContext(), 0);
    mlir::Value S = geomalg::BladeOp::create(Rewriter, Loc, ScalarT, Accum);
    NewOperands.insert(NewOperands.begin(), S);
  }

  // Sort for CSE.
  llvm::stable_sort(NewOperands, compareBlockValues);

  // Absorb the result used only by a single NegateOp so we do
  // not end up yielding a noncanonical blade type.
  mlir::Operation* OpToReplace = Op;
  mlir::Value Result = Op->getResult(0);
  mlir::Type ResultT = Op->getResult(0).getType();
  if (Result.hasOneUse()) {
    if (auto NOp = dyn_cast<NegateOp>(Result.use_begin()->getOwner())) {
      OpToReplace = NOp.getOperation();
      ResultT = NOp.getResult().getType();
      ++NegateCount;
    } else if (auto SWOp = dyn_cast<OSwapOp>(Result.use_begin()->getOwner())) {
      OpToReplace = SWOp.getOperation();
      ResultT = SWOp.getResult().getType();
      ++NegateCount;
    }
  }

  if (isZero(ResultT))
    return replaceWithZero(Rewriter, OpToReplace);

  mlir::Value NewResult;
  if (NewOperands.size() == 1 && NewOperands.front().getType() == ResultT)
    NewResult = NewOperands.front();
  else if (IsSingleBlade)
    NewResult = geomalg::BladeOp::create(Rewriter, Loc, ResultT, Accum);
  else
    NewResult = geomalg::CMulOp::create(Rewriter, Loc, ResultT, NewOperands);

  if (NegateCount % 2 != 0)
    NewResult = geomalg::NegateOp::create(Rewriter, Loc,
                                          ResultT, NewResult);
  replaceOp(Rewriter, OpToReplace, NewResult);
  return llvm::success();
}

llvm::LogicalResult SimplifySum::matchAndRewrite(
    SumOp Op,
    mlir::PatternRewriter& Rewriter) const {
  llvm::SmallVector<mlir::Value, 8> NewOperands(Op.getArgs().begin(),
                                                Op.getArgs().end());
  // Filter NewOperands by setting to mlir::Value().
  for (mlir::Value& V : NewOperands) {
    if (!V) continue;
    if (isZero(V)) {
      V = mlir::Value();
    } else if (auto NOp = V.getDefiningOp<NegateOp>()) {
      // Cancel negates if their operand is also an operand of the sum.
      // (ie Additive cancellation: A - A = 0)
      auto Itr = llvm::find(NewOperands, NOp.getArg());
      if (Itr != NewOperands.end()) {
        // Cancel by removing both values.
        V = mlir::Value();
        *Itr = mlir::Value();
      }
    }
  }

  llvm::erase(NewOperands, mlir::Value());

  // If operands are equal to the results of an Expand,
  // remove the unnecessary expand and sum altogether.
  if (!NewOperands.empty()) {
    if (auto ExOp = NewOperands.front().getDefiningOp<ExpandOp>()) {
      if (llvm::equal(NewOperands, ExOp.getResults())) {
        replaceOp(Rewriter, Op, ExOp.getArg());
        return llvm::success();
      }
    }
  }

  if (llvm::equal(NewOperands, Op.getArgs()))
    return llvm::failure();

  replaceOpWithNewOp<SumOp>(Rewriter, Op, NewOperands);

  return llvm::success();
}

llvm::LogicalResult SimplifyDot::matchAndRewrite(
    DotOp Op,
    mlir::PatternRewriter& Rewriter) const {
  if (Op.getLHS() != Op.getRHS())
    return llvm::failure();

  mlir::Value LHS = Op.getLHS();
  mlir::Value RHS = Op.getRHS();

  mlir::Type T = LHS.getType();
  if (LHS == RHS && (isUnitVector(LHS) || isUnitBlade(LHS))) {
    auto ScalarT = geomalg::BladeType::get(getContext(), 0);
    replaceOpWithNewOp<BladeOp>(Rewriter, Op, ScalarT, 1.0f);
    return llvm::success();
  }
  return llvm::failure();
}

llvm::LogicalResult ExpandPass::initialize(mlir::MLIRContext* Ctx) {
  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<Distribute>(Ctx, mlir::PatternBenefit(1));
  PS.add<ZeroAbsorbToZero,
         SimplifyInverse,
         SimplifyNegate,
         SimplifyDot>(Ctx, mlir::PatternBenefit(10));
  PS.add<ExpandLC,
         ExpandGP,
         RemoveExpand,
         RemoveCast,
         ExpandInverse,
         ExpandReverse,
         ExpandGradeInvo,
         ExpandConvert,
         UpdateInferredTypes>(Ctx, mlir::PatternBenefit(5));
  PS.add<DistributeVP>(Ctx, mlir::PatternBenefit(0));
  PS.add<ExpandVP>(geomalg::Metric::get(metric), Ctx,
                      mlir::PatternBenefit(2));
  if (metric != geomalg::MetricKind::unknown) {
    PS.add<ApplyMetric>(geomalg::Metric::get(metric), Ctx,
                        mlir::PatternBenefit(10));
  }
  Patterns = mlir::FrozenRewritePatternSet(std::move(PS),
                        disabledPatterns,
                        enabledPatterns);
  return llvm::success();
}

void ExpandPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  bool AnyIRChanged = true;
  while (AnyIRChanged) {
    AnyIRChanged = false;
    bool IRChanged = false;
    if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, Patterns,
                mlir::GreedyRewriteConfig(), &IRChanged)))
      return signalPassFailure();
    AnyIRChanged |= IRChanged;

    // Run CSE... maybe.
    if (IRChanged) {
      mlir::IRRewriter Rewriter(Ctx);
      mlir::DominanceInfo DI;
      mlir::eliminateCommonSubExpressions(Rewriter, DI, FuncOp, &IRChanged);
    }
    AnyIRChanged |= IRChanged;

    // TODO FrozenPatternify this
    mlir::RewritePatternSet PS(Ctx);
    PS.add<ExpandSum>(Ctx, mlir::PatternBenefit(0));
    if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, std::move(PS),
                mlir::GreedyRewriteConfig(), &IRChanged)))
      return signalPassFailure();
    AnyIRChanged |= IRChanged;

    // Run CSE.
    {
      mlir::IRRewriter Rewriter(Ctx);
      mlir::DominanceInfo DI;
      mlir::eliminateCommonSubExpressions(Rewriter, DI, FuncOp, &IRChanged);
    }
    AnyIRChanged |= IRChanged;
  }
}

llvm::LogicalResult SimplifyPass::initialize(mlir::MLIRContext* Ctx) {
  // Create pattern rewriter thingy.
  mlir::RewritePatternSet PS(Ctx);
  PS.add<ZeroAbsorbToZero>(Ctx, mlir::PatternBenefit(10));
  PS.add<SimplifyMul,
         SimplifySum,
         SimplifyInverse,
         SimplifyNegate,
         SimplifyDot
         >(Ctx);
  Patterns = mlir::FrozenRewritePatternSet(std::move(PS),
                        disabledPatterns,
                        enabledPatterns);
  return llvm::success();
}

void SimplifyPass::runOnOperation() {
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();

  bool IRChanged = true;
  while (IRChanged) {
    // Run Patterns.
    if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, Patterns)))
      return signalPassFailure();

    // Run CSE.
    {
      mlir::IRRewriter Rewriter(Ctx);
      mlir::DominanceInfo DI;
      mlir::eliminateCommonSubExpressions(Rewriter, DI, FuncOp, &IRChanged);
    }
  }
}

llvm::LogicalResult MatrixPass::initialize(mlir::MLIRContext* Ctx) {
  return llvm::success();
}

void MatrixPass::runOnOperation() {
  // Create a new region for the body with the same arguments
  // and insert a new matmul and return operation.
  mlir::MLIRContext* Ctx = &getContext();
  mlir::func::FuncOp FuncOp = getOperation();
  mlir::Location Loc = FuncOp.getLoc();
  // TODO Use ExpandMatmul.
  //PS.add<ExpandMatmul>(Ctx, mlir::PatternBenefit(6));

  mlir::Region& Body = FuncOp.getBody();
  auto NewBody = std::make_unique<mlir::Region>(FuncOp);
  NewBody->emplaceBlock();
  for (mlir::Type ArgT : FuncOp.getArgumentTypes())
    NewBody->addArgument(ArgT, Loc);
  auto BodyBuilder = mlir::OpBuilder(NewBody.get());

  mlir::Value InputArg = !Body.getArguments().empty()
        ? mlir::Value(Body.getArguments().front()) : mlir::Value();
  if (!InputArg || !isa<MultivectorLike>(InputArg.getType())) {
    FuncOp->emitError("expecting an input argument");
    return signalPassFailure();
  }
  MultivectorLike MVL = cast<MultivectorLike>(InputArg.getType());
  mlir::Value NewInputArg = NewBody->getArguments().front();

  llvm::ArrayRef<BladeType> Blades = MVL.getBlades();

  unsigned NumRegions = MVL.getBlades().size();
  auto MM = MatmulOp::create(BodyBuilder, Loc, NewInputArg,
                             /*NumRegions=*/MVL.getBlades().size());
  mlir::Value Result = MM.getResult();
  mlir::Block& BB = NewBody->front();
  ReturnOp::create(BodyBuilder, Loc, MM.getResult());

  // Instantiate the regions of the new MatmulOp.
  for (auto&& [Region, BladeT] : llvm::zip(MM.getBodies(), MVL.getBlades())) {
    // Instantiate body substituting InputArg
    // with the basis element for this blade.
    mlir::OpBuilder Builder = mlir::OpBuilder(MM);
    mlir::Value BasisElement = BladeOp::create(Builder, Loc, BladeT, 1.0f);
    mlir::IRMapping IRMap;
    IRMap.map(InputArg, BasisElement);
    Body.cloneInto(&Region, IRMap);
  }

  // TODO Use PatternRewriters to fix ExpandOps and simplify zeros.


  // Finish by replacing the original body with the new one.
  Body.takeBody(*NewBody);
}
