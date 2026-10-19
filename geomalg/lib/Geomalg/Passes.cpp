#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <llvm/ADT/STLExtras.h>
#include <mlir/IR/Dominance.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/CSE.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Transforms/Passes.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_EXPANDFUNCPASS
#define GEN_PASS_DEF_EXPANDPASS
#define GEN_PASS_DEF_SIMPLIFYPASS
#define GEN_PASS_DEF_MATRIXPASS
#include "geomalg/GeomalgPasses.h.inc"
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

// This actually lifts to a multivector type, blade type, or zero type.
// Arg blades should be a subset of TargetT blades.
static mlir::Value
liftToMultivector(mlir::PatternRewriter& Rewriter, mlir::Type TargetT,
                  mlir::Value Arg) {
  mlir::Location Loc = Arg.getLoc();
  mlir::Type ArgT = Arg.getType();

  if (isZero(TargetT))
    return geomalg::BladeOp::create(Rewriter, Loc, TargetT, 0);

  if (ArgT == TargetT || isLikeMultivector(ArgT, TargetT))
    return Arg;

  llvm::ArrayRef<BladeType> Blades;
  auto MaybeBT = dyn_cast<BladeType>(TargetT);
  if (MaybeBT)
    Blades = MaybeBT;
  else if (auto MVL = dyn_cast<MultivectorLike>(TargetT))
    Blades = MVL.getBlades();
  else
    llvm_unreachable("expecting blade or multivector type");

  if (MaybeBT && Blades.size() == 1)
    return Arg;

  mlir::ValueRange Args;
  if (isa<MultivectorLike>(ArgT))
    Args = expandMultivector(Rewriter, Arg);
  else
    Args = mlir::ValueRange(Arg);

  assert(llvm::all_of(Args, [&](mlir::Value A) {
        auto BT = dyn_cast<BladeType>(A.getType());
        bool IsZero = isZero(A.getType());
        return (!BT && IsZero) || (BT && llvm::is_contained(Blades, BT));
        }) &&
      "expecting subset of blades in target");


  llvm::SmallVector<mlir::Value, 8> SumArgs(Blades.size());
  for (auto [BT, SumArg] : llvm::zip(Blades, SumArgs)) {
    auto Pred = [BT](mlir::Value A) { return A.getType() == BT; };
    auto Itr = llvm::find_if(Args, Pred);
    if (Itr != Args.end())
      SumArg = *Itr;
    else
      SumArg = BladeOp::create(Rewriter, Loc, BT, 0.0f);
  }
  mlir::Value Result = SumOp::create(Rewriter, Loc, SumArgs);
  return Result;
}

// Get the 1-vector factors of a versor in product order.
static llvm::SmallVector<mlir::Value, 4>
getVersorFactors(mlir::PatternRewriter& Rewriter, mlir::Value Versor) {
  assert(isa<geomalg::VersorType>(Versor.getType()) &&
      "expecting versor type");
  llvm::SmallVector<mlir::Value, 4> Factors;
  if (auto VOp = Versor.getDefiningOp<geomalg::VersorOp>()) {
    for (mlir::Value F : VOp.getFactors()) {
      if (isa<geomalg::VersorType>(F.getType()))
        llvm::append_range(Factors, getVersorFactors(Rewriter, F));
      else
        Factors.push_back(F);
    }
    return Factors;
  }

  // Prevent duplicate FactorsOps.
  auto FactorsOpItr = llvm::find_if(Versor.getUsers(),
      [](mlir::Operation* Op) {
        return llvm::isa<geomalg::FactorsOp>(Op);
      });
  geomalg::FactorsOp FOp;
  if (FactorsOpItr != Versor.getUsers().end()) {
    FOp = cast<geomalg::FactorsOp>(*FactorsOpItr);
  } else {
    mlir::OpBuilder::InsertionGuard IG(Rewriter);
    Rewriter.setInsertionPointAfterValue(Versor);
    FOp = geomalg::FactorsOp::create(Rewriter, Versor.getLoc(), Versor);
  }
  llvm::append_range(Factors, FOp.getResults());
  return Factors;
}

// Get the blade values of a blade or multivector.
static llvm::SmallVector<mlir::Value, 8>
getBladeValues(mlir::PatternRewriter& Rewriter, mlir::Value V) {
  if (isa<geomalg::MultivectorLike>(V.getType()))
    return llvm::SmallVector<mlir::Value, 8>(expandMultivector(Rewriter, V));
  return {V};
}

// Sum nonempty values without creating a sum of a single value.
static mlir::Value createSum(mlir::PatternRewriter& Rewriter,
                             mlir::Location Loc, mlir::ValueRange Vs) {
  assert(!Vs.empty() && "expecting values to sum");
  if (Vs.size() == 1)
    return Vs.front();
  return geomalg::SumOp::create(Rewriter, Loc, Vs);
}

namespace {
// Build scalar arithmetic with geomalg operations.
struct ScalarBuilder {
  mlir::PatternRewriter& Rewriter;
  mlir::Location Loc;
  mlir::Type ScalarT;

  ScalarBuilder(mlir::PatternRewriter& R, mlir::Location L)
    : Rewriter(R), Loc(L), ScalarT(geomalg::BladeType::get(R.getContext(), 0))
  { }

  mlir::Value constant(float C) {
    return geomalg::BladeOp::create(Rewriter, Loc, ScalarT, C);
  }
  // Multiply a scalar by a scalar or multivector.
  mlir::Value mul(mlir::Value A, mlir::Value B) {
    return geomalg::InnerProdOp::create(Rewriter, Loc, A, B);
  }
  mlir::Value add(mlir::Value A, mlir::Value B) {
    return geomalg::SumOp::create(Rewriter, Loc, A, B);
  }
  mlir::Value neg(mlir::Value A) {
    return geomalg::NegateOp::create(Rewriter, Loc, A);
  }
  mlir::Value sub(mlir::Value A, mlir::Value B) {
    return add(A, neg(B));
  }
  mlir::Value inv(mlir::Value A) {
    return geomalg::InverseOp::create(Rewriter, Loc, A);
  }
  mlir::Value div(mlir::Value A, mlir::Value B) {
    return mul(A, inv(B));
  }
  mlir::Value sqrt(mlir::Value A) {
    return geomalg::SqrtOp::create(Rewriter, Loc, ScalarT, A);
  }
  mlir::Value sin(mlir::Value A) {
    return geomalg::SinOp::create(Rewriter, Loc, ScalarT, A);
  }
  mlir::Value cos(mlir::Value A) {
    return geomalg::CosOp::create(Rewriter, Loc, ScalarT, A);
  }
  mlir::Value atan2(mlir::Value Y, mlir::Value X) {
    return geomalg::Atan2Op::create(Rewriter, Loc, ScalarT, Y, X);
  }
  // Select T if L < R or F otherwise.
  mlir::Value selectLt(mlir::Value L, mlir::Value R,
                       mlir::Value T, mlir::Value F) {
    return geomalg::SelectLtOp::create(Rewriter, Loc, ScalarT, L, R, T, F);
  }
};
}  // namespace

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

template <typename Base_>
struct RewriteMetricBase : Base_ {
  using Base = RewriteMetricBase;

  // TODO Store as reference.
  geomalg::Metric Metric;

  template <typename ...Args>
  RewriteMetricBase(geomalg::Metric M, Args&& ...args)
    : Base_(std::forward<Args>(args)...)
    , Metric(M)
  { }
};

template <typename OpTy>
using OpRewriteMetric = RewriteMetricBase<mlir::OpRewritePattern<OpTy>>;

template <template <typename> class OpTraitTy>
using OpTraitRewriteMetric
  = RewriteMetricBase<mlir::OpTraitRewritePattern<OpTraitTy>>;

// Rewriters for expansion.

struct RemoveConvert : mlir::OpRewritePattern<geomalg::ConvertOp> {
  using Base = mlir::OpRewritePattern<geomalg::ConvertOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("RemoveConvert");
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
struct ExpandMatvec : mlir::OpTraitRewritePattern<Multilinear> {
  using Base = mlir::OpTraitRewritePattern<Multilinear>;
  using Base::OpTraitRewritePattern;

  void initialize() {
    setDebugName("ExpandMatvec");
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

// Replace each versor operand of a versor product with its factors.
struct ExpandVersorVP : mlir::OpRewritePattern<geomalg::VersorProdOp> {
  using Base = mlir::OpRewritePattern<geomalg::VersorProdOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandVersorVP");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const override;
};

// Rewrite the inverse, reverse, grade involution, or negation
// of a versor as a versor.
struct ExpandVersorUnary : mlir::RewritePattern {
  ExpandVersorUnary(mlir::MLIRContext* Ctx, mlir::PatternBenefit Benefit)
    : mlir::RewritePattern(MatchAnyOpTypeTag(), Benefit, Ctx)
  { }

  void initialize() {
    setDebugName("ExpandVersorUnary");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Replace versor operands of other operations with the geometric
// product of their factors.
struct ExpandVersorOperand : mlir::RewritePattern {
  ExpandVersorOperand(mlir::MLIRContext* Ctx, mlir::PatternBenefit Benefit)
    : mlir::RewritePattern(MatchAnyOpTypeTag(), Benefit, Ctx)
  { }

  void initialize() {
    setDebugName("ExpandVersorOperand");
  }

  llvm::LogicalResult matchAndRewrite(
      mlir::Operation* Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Keep the blades of a multivector of the given grade.
struct ExpandGrade : mlir::OpRewritePattern<geomalg::GradeOp> {
  using Base = mlir::OpRewritePattern<geomalg::GradeOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ExpandGrade");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::GradeOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the exponential of a bivector to a motor.
struct ExpandExp : OpRewriteMetric<geomalg::ExpOp> {
  using Base::Base;

  void initialize() {
    setDebugName("ExpandExp");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::ExpOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

// Expand the logarithm of a motor to a bivector.
struct ExpandLog : OpRewriteMetric<geomalg::LogOp> {
  using Base::Base;

  void initialize() {
    setDebugName("ExpandLog");
  }

  llvm::LogicalResult matchAndRewrite(
      geomalg::LogOp Op,
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
struct ExpandInverse : OpRewriteMetric<geomalg::InverseOp> {
  using Base::Base;

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

struct SimplifyDoubling : mlir::OpRewritePattern<SumOp> {
  using Base = mlir::OpRewritePattern<SumOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifyDoubling");
  }

  llvm::LogicalResult matchAndRewrite(
      SumOp Op,
      mlir::PatternRewriter& Rewriter) const override;
};

struct SimplifySum : mlir::OpRewritePattern<geomalg::SumOp> {
  using Base = mlir::OpRewritePattern<geomalg::SumOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("SimplifySum");
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

// Given a CallOp and its corresponding callee FuncOp,
// emit an error if known call argument types do not match exactly.
llvm::LogicalResult checkCallArgs(mlir::func::FuncOp FuncOp,
                                  geomalg::CallOp CallOp) {
  mlir::TypeRange ParamTs = FuncOp.getArgumentTypes();
  mlir::TypeRange ArgTs = CallOp->getOperandTypes();
  if (ParamTs.size() != ArgTs.size()) {
    CallOp->emitError("call arguments do not match callee arity");
    return llvm::failure();
  }
  for (auto [ParamT, ArgT] : llvm::zip(ParamTs, ArgTs)) {
    if (!isUnknown(ArgT) && ArgT != ParamT) {
      CallOp->emitError("call arguments do not match function prototype");
      return llvm::failure();
    }
  }
  return llvm::success();
}


// Walk the body of the FuncOp to check nested CallOps
// emitting an error on failure.
llvm::LogicalResult checkCallArgs(mlir::func::FuncOp ParentFuncOp) {
  bool Result = true;
  auto ModuleOp = ParentFuncOp->getParentOfType<mlir::ModuleOp>();
  ParentFuncOp->walk([&](geomalg::CallOp CallOp)
      -> mlir::WalkResult {
    mlir::FlatSymbolRefAttr CalleeSym = CallOp.getCalleeAttr();
    mlir::Operation* LookupOp = ModuleOp.lookupSymbol(CalleeSym.getAttr());
    auto FuncOp = dyn_cast_or_null<mlir::func::FuncOp>(LookupOp);
    if (!FuncOp)
      CallOp->emitError("callee is invalid");
    else if (llvm::succeeded(checkCallArgs(FuncOp, CallOp)))
      return mlir::WalkResult::advance();

    Result = false;
    return mlir::WalkResult::interrupt();
  });
  return llvm::success(Result);
}

static mlir::Value square(mlir::PatternRewriter& Rewriter,
                          geomalg::Metric const& Metric,
                          mlir::Location Loc, mlir::Value MV) {
  mlir::Value Squared;
  auto MVL = dyn_cast<MultivectorLike>(MV.getType());
  if (false && MVL && Metric && isOrthogonalBasis(Metric, MVL, MVL)) {
    mlir::Type ScalarT = geomalg::BladeType::get(MV.getContext(), 0);
    Squared = geomalg::DotOp::create(Rewriter, Loc, ScalarT, MV, MV);
  } else {
    Squared = geomalg::InnerProdOp::create(Rewriter, Loc, MV, MV);
  }
  return Squared;
}

static mlir::Value squareInverse(
                          mlir::PatternRewriter& Rewriter,
                          geomalg::Metric const& Metric,
                          mlir::Location Loc, mlir::Value MV) {
  mlir::Value Squared = square(Rewriter, Metric, Loc, MV);
  return geomalg::InverseOp::create(Rewriter, Loc, Squared);
}

struct ExpandConvert : OpRewriteMetric<geomalg::ConvertOp> {
  using Base::Base;

  void initialize() {
    setDebugName("ExpandConvert");
  }

  llvm::LogicalResult matchAndRewrite(
                        geomalg::ConvertOp Op,
                        mlir::PatternRewriter& Rewriter) const override {
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

      // Multiply the inverse of norm squared and the Arg vector.
      mlir::Value SquareInverse = squareInverse(Rewriter, Metric, Loc, Arg);
      mlir::Value IP = InnerProdOp::create(Rewriter, Loc, SquareInverse, Arg);
      replaceOpWithNewOp<CastOp>(Rewriter, Op, ResultT, IP);
      return llvm::success();
    }

    // Widen a versor by converting each of its factors.
    if (auto VT = dyn_cast<VersorType>(ResultT);
        VT && isa<VersorType>(ArgT) && isValidNarrowing(ResultT, ArgT)) {
      llvm::SmallVector<mlir::Value, 4> Factors
        = getVersorFactors(Rewriter, Arg);
      if (Factors.size() != VT.getFactors().size())
        return llvm::failure();
      for (auto [F, FT] : llvm::zip(Factors, VT.getFactors()))
        if (F.getType() != FT)
          F = ConvertOp::create(Rewriter, Loc, FT, F);
      replaceOpWithNewOp<VersorOp>(Rewriter, Op, Factors);
      return llvm::success();
    }

    // Widen to a type that the argument type is a narrowing of
    // by adding the missing blades with zero coefficients.
    // (e.g. to keep the result type of a function.)
    if (!isUnknown(ArgT) && isValidNarrowing(ResultT, ArgT)) {
      mlir::Value Widened;
      if (auto BT = dyn_cast<BladeType>(ResultT)) {
        assert(isZero(ArgT) && "expecting zero to widen to a blade");
        Widened = BladeOp::create(Rewriter, Loc, BT, 0.0f);
      } else {
        Widened = liftToMultivector(Rewriter, ResultT, Arg);
        // The blades are the same but the type may not be. (e.g. unit_vector)
        if (Widened.getType() != ResultT)
          Widened = CastOp::create(Rewriter, Loc, ResultT, Widened);
      }
      replaceOp(Rewriter, Op, Widened);
      return llvm::success();
    }

    // ConvertOps must eventually be removed or expanded, but not
    // necessarily at this point. (See RemoveConvert.)
    return llvm::failure();
  }
};

// Convert each operand of Op that was narrowed from the type required
// of it back to the required type. (See ExpandConvert.)
static llvm::LogicalResult
convertNarrowedOperands(mlir::PatternRewriter& Rewriter, mlir::Operation* Op,
                        llvm::MutableArrayRef<mlir::OpOperand> Operands,
                        mlir::TypeRange RequiredTs) {
  bool Changed = false;
  for (auto [Operand, RequiredT] : llvm::zip(Operands, RequiredTs)) {
    mlir::Value Arg = Operand.get();
    mlir::Type ArgT = Arg.getType();
    if (ArgT == RequiredT || isUnknown(ArgT) || isUnknown(RequiredT) ||
        !isValidNarrowing(RequiredT, ArgT))
      continue;
    auto Convert = ConvertOp::create(Rewriter, Op->getLoc(), RequiredT, Arg);
    Rewriter.modifyOpInPlace(Op, [&] { Operand.set(Convert); });
    Changed = true;
  }
  return llvm::success(Changed);
}

// Keep the result types of a function when the values returned
// are narrowed. (This includes the returns of other dialects.)
struct ConvertNarrowedReturn
    : mlir::OpInterfaceRewritePattern<schir::InferableReturn> {
  using Base = mlir::OpInterfaceRewritePattern<schir::InferableReturn>;
  using Base::OpInterfaceRewritePattern;

  void initialize() {
    setDebugName("ConvertNarrowedReturn");
  }

  llvm::LogicalResult matchAndRewrite(
      schir::InferableReturn Op,
      mlir::PatternRewriter& Rewriter) const override {
    mlir::FunctionOpInterface FuncOp = Op.getReturnedFunction();
    if (!FuncOp)
      return llvm::failure();
    llvm::ArrayRef<mlir::Type> ResultTs = FuncOp.getResultTypes();
    llvm::MutableArrayRef<mlir::OpOperand> Operands = Op.getReturnedOperands();
    if (ResultTs.size() != Operands.size())
      return llvm::failure();
    return convertNarrowedOperands(Rewriter, Op, Operands, ResultTs);
  }
};

// Keep the parameter types of a callee when the call
// arguments are narrowed.
struct ConvertNarrowedCallArgs : mlir::OpRewritePattern<CallOp> {
  using Base = mlir::OpRewritePattern<CallOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("ConvertNarrowedCallArgs");
  }

  llvm::LogicalResult matchAndRewrite(
      CallOp Op, mlir::PatternRewriter& Rewriter) const override {
    auto FuncOp = mlir::SymbolTable::lookupNearestSymbolFrom<
                                    mlir::func::FuncOp>(Op, Op.getCalleeAttr());
    if (!FuncOp)
      return llvm::failure();
    return convertNarrowedOperands(Rewriter, Op, Op->getOpOperands(),
                                   FuncOp.getArgumentTypes());
  }
};

// Remove ConvertOps or throw an error if there is not conversion.
llvm::LogicalResult
RemoveConvert::matchAndRewrite(geomalg::ConvertOp Op,
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

  return Rewriter.notifyMatchFailure(Op, "no known conversion");
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
    } else if (isUnknown(V) || isa<VersorType>(V.getType())) {
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
    // Keep blades of outer products canonical as with createOuterProd.
    if (auto BT = dyn_cast<BladeType>(Result.getType());
        BT && isa<OuterProdOp>(NewOp) && !BT.isCanonical())
      Result = OSwapOp::create(Rewriter, NewOp->getLoc(), Result);
    Results.push_back(Result);
  }

  // Replace the original op with a shiny, new SumOp.
  bool IsUnit = isa<UnitVectorType>(Op->getResult(0).getType());
  replaceOpWithNewOp<geomalg::SumOp>(Rewriter, Op, IsUnit, Results);

  return llvm::success();
}

llvm::LogicalResult
ExpandMatvec::matchAndRewrite(mlir::Operation* Op,
                              mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  mlir::Location Loc = Op->getLoc();

  // A matrix for operations like Negate is not so useful.
  if (Op->getNumOperands() < 2)
    return llvm::failure();

  assert(Op->getResults().size() == 1);
  if (!isa<MultivectorLike>(Op->getResult(0).getType()) &&
      !isUnknown(Op->getResult(0)))
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
  auto MM = MatvecOp::create(Rewriter, Loc, MV,
                             /*NumRegions=*/Blades.size());

  // Instantiate the regions of the new MatvecOp.
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

struct UpdateMatvecReturn : mlir::OpRewritePattern<ReturnOp> {
  using Base = mlir::OpRewritePattern<ReturnOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("UpdateMatvecReturn");
  }

  llvm::LogicalResult matchAndRewrite(ReturnOp Op,
                        mlir::PatternRewriter& Rewriter) const override {
    mlir::Value RetArg = Op.getArg();
    mlir::Type RetT = RetArg.getType();

    // The target result type.
    mlir::Type ResultT;
    if (auto MM = dyn_cast<MatvecOp>(Op->getParentOp()))
      ResultT = MM.getResult().getType();

    if (!ResultT || isUnknown(ResultT) || isUnknown(RetT) ||
        RetT == ResultT || isLikeMultivector(ResultT, RetT))
      return llvm::failure(); // Nothing more to do here

    // Make all region result types the same as the MatvecOp result type.
    mlir::Value NewArg = liftToMultivector(Rewriter, ResultT, RetArg);
    if (NewArg == RetArg)
      return llvm::failure();
    Rewriter.replaceOpWithNewOp<ReturnOp>(Op, NewArg);
    return llvm::success();
  }
};

// Update the function result type if it is different from
// the ReturnOp argument type.
struct UpdateFuncReturnType : mlir::OpRewritePattern<mlir::func::FuncOp> {
  using Base = mlir::OpRewritePattern<mlir::func::FuncOp>;
  using Base::OpRewritePattern;

  llvm::LogicalResult matchAndRewrite(mlir::func::FuncOp FuncOp,
                        mlir::PatternRewriter& Rewriter) const override {
    mlir::FunctionType FT = FuncOp.getFunctionType();
    if (FT.getNumResults() != 1)
      return llvm::failure();

    auto ReturnOp = dyn_cast<geomalg::ReturnOp>(
        FuncOp.getBody().front().getTerminator());
    if (!ReturnOp)
      return llvm::failure();

    mlir::Type ResultT = FT.getResult(0);
    mlir::Type ReturnT = ReturnOp.getArg().getType();

    if (ResultT == ReturnT)
      return llvm::failure();

    // The return argument must match an explicit
    // function return type exactly.
    if (!isUnknown(ResultT)) {
      FuncOp->emitError("return type does not match function prototype");
      return llvm::failure();
    }

    mlir::FunctionType NewFT = mlir::FunctionType::get(
        getContext(), FT.getInputs(), ReturnT);
    Rewriter.startOpModification(FuncOp);
    FuncOp.setFunctionType(NewFT);
    Rewriter.finalizeOpModification(FuncOp);

    // Update relevant CallOp in ExpandPass loop.

    return llvm::success();
  }
};

struct UpdateCall : mlir::OpRewritePattern<CallOp> {
  using Base = mlir::OpRewritePattern<CallOp>;
  using Base::OpRewritePattern;

  void initialize() {
    setDebugName("UpdateCall");
  }

  llvm::LogicalResult matchAndRewrite(CallOp Op,
                        mlir::PatternRewriter& Rewriter) const override {
    return llvm::failure();
    auto ModuleOp = Op->getParentOfType<mlir::ModuleOp>();
    mlir::FlatSymbolRefAttr CalleeSym = Op.getCalleeAttr();
    mlir::Operation* LookupOp = ModuleOp.lookupSymbol(CalleeSym.getAttr());
    auto FuncOp = dyn_cast_or_null<mlir::func::FuncOp>(LookupOp);
    if (!FuncOp)
      return llvm::failure();

    mlir::FunctionType FT = FuncOp.getFunctionType();
    assert(FT.getNumResults() == 1);
    mlir::Type ResultT = FT.getResult(0);
    mlir::Type CallResultT = Op.getResult().getType();

    mlir::TypeRange ArgTs = FuncOp.getFunctionType().getInputs();
    mlir::TypeRange OpArgTs = Op->getOperandTypes();

    return setResultType(Rewriter, Op.getOperation(), ResultT);
  }
};

llvm::LogicalResult
UpdateInferredTypes::matchAndRewrite(mlir::InferTypeOpInterface Op,
                            mlir::PatternRewriter& Rewriter) const {
  mlir::MLIRContext* Ctx = getContext();
  if (Op->getNumResults() != 1 ||
      !isUnknown(Op->getResult(0)))
    return llvm::failure();

  // Is the inferred type still unknown?
  mlir::Type ResultT = inferReturnType(Op);
  if (isUnknown(ResultT))
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
  if (isUnknown(ExpandOp.getArg()))
    return llvm::failure();

  // Replace unnecessary ExpandOps.
  if (auto SOp = ExpandOp.getArg().getDefiningOp<SumOp>()) {
    replaceOp(Rewriter, ExpandOp, SOp.getOperands());
    return llvm::success();
  }

  // Replace Expand of Cast. (e.g. Expanding cast to unit_vector)
  if (auto COp = ExpandOp.getArg().getDefiningOp<CastOp>()) {
    auto AMV = dyn_cast<MultivectorLike>(COp.getArg().getType());
    auto RMV = dyn_cast<MultivectorLike>(COp.getResult().getType());
    if (AMV && RMV && llvm::equal(AMV.getBlades(), RMV.getBlades())) {
      replaceOpWithNewOp<geomalg::ExpandOp>(Rewriter,
                                    ExpandOp, COp.getArg());
      return llvm::success();
    }
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

  // Apply the remaining versors to the sum so its terms are combined
  // before they are distributed again.
  llvm::SmallVector<mlir::Value, 8> SumArgs;
  mlir::ValueRange Values = expandMultivector(Rewriter, Arg);
  for (mlir::Value V : Values) {
    mlir::Value SumArg = VersorProdOp::create(Rewriter, Loc, V,
                                              Versors.take_front());
    SumArgs.push_back(SumArg);
  }
  mlir::Value Result = SumOp::create(Rewriter, Loc, SumArgs);
  if (Versors.size() > 1)
    Result = VersorProdOp::create(Rewriter, Loc, Result, Versors.drop_front());
  replaceOp(Rewriter, VP, Result);

  return llvm::success();
}

// Get ⟨V Ṽ⟩₀ as the sum of the scalar products of the grade parts of V.
static mlir::Value createVersorNormSq(mlir::PatternRewriter& Rewriter,
                                      mlir::Location Loc, mlir::Value V) {
  llvm::SmallVector<mlir::Value, 8> Terms;
  llvm::SmallVector<mlir::Value, 8> Blades = getBladeValues(Rewriter, V);
  llvm::SmallVector<mlir::Value, 8> Part;
  for (unsigned Grade = 0; !Blades.empty(); ++Grade) {
    Part.clear();
    llvm::erase_if(Blades, [&](mlir::Value B) {
      if (cast<BladeType>(B.getType()).getGrade() != Grade)
        return false;
      Part.push_back(B);
      return true;
    });
    if (Part.empty())
      continue;
    // ⟨V⟩ₖ ⌋ ⟨Ṽ⟩ₖ = ± ⟨V⟩ₖ ⌋ ⟨V⟩ₖ
    mlir::Value P = createSum(Rewriter, Loc, Part);
    mlir::Value Term = InnerProdOp::create(Rewriter, Loc, P, P);
    if (Grade % 4 >= 2)
      Term = NegateOp::create(Rewriter, Loc, Term);
    Terms.push_back(Term);
  }
  return createSum(Rewriter, Loc, Terms);
}

// Apply a versor that is a multivector to a blade.
// (ie V A V⁻¹ = V A Ṽ / ⟨V Ṽ⟩₀)
static mlir::Value expandMultivectorVP(mlir::PatternRewriter& Rewriter,
                                       mlir::Location Loc,
                                       mlir::Value Arg, mlir::Value Versor,
                                       bool IsOrthogonal,
                                       llvm::ArrayRef<BladeType> VersorBTs) {
  // An orthogonal blade commutes or anticommutes with a versor
  // of a single parity.
  auto Odd = [](BladeType BT) { return BT.getGrade() % 2 != 0; };
  unsigned ArgGrade = cast<BladeType>(Arg.getType()).getGrade();
  if (IsOrthogonal && llvm::all_equal(llvm::map_range(VersorBTs, Odd))) {
    if (Odd(VersorBTs.front()) && ArgGrade % 2 != 0)
      return NegateOp::create(Rewriter, Loc, Arg);
    return Arg;
  }

  mlir::Value Rev = ReverseOp::create(Rewriter, Loc, Versor);
  mlir::Value ArgRev = GeomProdOp::create(Rewriter, Loc, Arg, Rev);
  mlir::Value Sandwich = GeomProdOp::create(Rewriter, Loc, Versor, ArgRev);
  mlir::Value NormSq = createVersorNormSq(Rewriter, Loc, Versor);
  mlir::Value InvNormSq = InverseOp::create(Rewriter, Loc, NormSq);
  mlir::Value Result = InnerProdOp::create(Rewriter, Loc, InvNormSq, Sandwich);
  return GradeOp::create(Rewriter, Loc,
                         UnknownType::get(Rewriter.getContext()),
                         Result, ArgGrade);
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

  for (mlir::Value V : Versors) {
    if (isUnknown(V) || isa<VersorType>(V.getType()))
      return llvm::failure();
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

  // v₁ A v₁⁻¹
  mlir::Value Result;
  if (!SingleVBT && !isVector(Versor.getType())) {
    Result = expandMultivectorVP(Rewriter, Loc, Arg, Versor,
                                 IsOrthogonal, VersorBladeTypes);
  } else if (BT.getGrade() == 0 ||
             (IsOrthogonal && BT.getGrade() % 2 == 0)) {
    // Scalars and orthogonal blades of even grade commute with the versor.
    Result = Arg;
  } else if (IsOrthogonal) {
    // Use the antisymmetric property.
    mlir::Value VL = Negate(GradeInvo(Versor));
    mlir::Value Dot = InnerProdOp::create(Rewriter, Loc, VL, Inverse(Versor));
    Result = InnerProdOp::create(Rewriter, Loc, Dot, Arg); // scalar mult
    Result = NegateOp::create(Rewriter, Loc, Result.getType(), Result);
  } else if (isVector(Versor.getType()) && BT.getGrade() == 1) {
    // v a v⁻¹ = 2 (v ⌋ a) v⁻¹ - a
    mlir::Value Two = BladeOp::create(Rewriter, Loc, 2.0f);
    mlir::Value Dot = InnerProdOp::create(Rewriter, Loc, Versor, Arg);
    mlir::Value Scale = InnerProdOp::create(Rewriter, Loc, Two, Dot);
    Result = SumOp::create(Rewriter, Loc,
                           InnerProdOp::create(Rewriter, Loc, Scale,
                                               Inverse(Versor)),
                           Negate(Arg));
  } else if (isVector(Versor.getType())) {
    // v (a ∧ B) v⁻¹ = (v a v⁻¹) ∧ (v B v⁻¹)
    auto [a, B] = factorBlade(Rewriter, Arg);
    Result = createOuterProd(Rewriter, Loc,
                             VersorProdOp::create(Rewriter, Loc, a, Versor),
                             VersorProdOp::create(Rewriter, Loc, B, Versor));
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

// A versor V = f₁ f₂ ⋯ fₖ applies its last factor first.
// (ie V X V⁻¹ = f₁ (⋯ (fₖ X fₖ⁻¹) ⋯) f₁⁻¹)
llvm::LogicalResult ExpandVersorVP::matchAndRewrite(
      geomalg::VersorProdOp VP,
      mlir::PatternRewriter& Rewriter) const {
  mlir::ValueRange Versors = VP.getVersors();
  if (llvm::none_of(Versors.getTypes(), llvm::IsaPred<VersorType>))
    return llvm::failure();

  llvm::SmallVector<mlir::Value, 8> NewVersors;
  for (mlir::Value V : Versors) {
    if (isa<VersorType>(V.getType()))
      llvm::append_range(NewVersors,
                         llvm::reverse(getVersorFactors(Rewriter, V)));
    else
      NewVersors.push_back(V);
  }

  replaceOpWithNewOp<VersorProdOp>(Rewriter, VP, VP.getArg(), NewVersors);
  return llvm::success();
}

llvm::LogicalResult ExpandVersorUnary::matchAndRewrite(
      mlir::Operation* Op,
      mlir::PatternRewriter& Rewriter) const {
  if (!isa<InverseOp, ReverseOp, GradeInvoOp, NegateOp>(Op) ||
      !isa<VersorType>(Op->getOperand(0).getType()))
    return llvm::failure();

  mlir::Location Loc = Op->getLoc();
  llvm::SmallVector<mlir::Value, 4> Factors
    = getVersorFactors(Rewriter, Op->getOperand(0));
  llvm::SmallVector<mlir::Value, 4> NewFactors;
  if (isa<InverseOp>(Op)) {
    // (f₁ ⋯ fₖ)⁻¹ = fₖ⁻¹ ⋯ f₁⁻¹
    for (mlir::Value F : llvm::reverse(Factors))
      NewFactors.push_back(InverseOp::create(Rewriter, Loc, F));
  } else if (isa<ReverseOp>(Op)) {
    // (f₁ ⋯ fₖ)~ = fₖ ⋯ f₁
    llvm::append_range(NewFactors, llvm::reverse(Factors));
  } else if (isa<GradeInvoOp>(Op)) {
    // (f₁ ⋯ fₖ)^ = f₁^ ⋯ fₖ^
    for (mlir::Value F : Factors)
      NewFactors.push_back(GradeInvoOp::create(Rewriter, Loc, F));
  } else {
    // -(f₁ ⋯ fₖ) = (-f₁) ⋯ fₖ
    NewFactors = Factors;
    NewFactors.front() = NegateOp::create(Rewriter, Loc, Factors.front());
  }

  replaceOpWithNewOp<VersorOp>(Rewriter, Op, NewFactors);
  return llvm::success();
}

llvm::LogicalResult ExpandVersorOperand::matchAndRewrite(
      mlir::Operation* Op,
      mlir::PatternRewriter& Rewriter) const {
  if (!isa<SumOp, OuterProdOp, InnerProdOp, GeomProdOp,
           DotOp, VersorProdOp, LogOp>(Op))
    return llvm::failure();

  // The versors of a versor product are expanded by ExpandVersorVP.
  unsigned NumOperands = isa<VersorProdOp>(Op) ? 1 : Op->getNumOperands();
  bool Changed = false;
  for (mlir::OpOperand& Operand : Op->getOpOperands().take_front(NumOperands)) {
    mlir::Value V = Operand.get();
    if (!isa<VersorType>(V.getType()))
      continue;

    // f₁ f₂ ⋯ fₖ
    llvm::SmallVector<mlir::Value, 4> Factors = getVersorFactors(Rewriter, V);
    mlir::Value Product = Factors.front();
    for (mlir::Value F : llvm::drop_begin(Factors))
      Product = GeomProdOp::create(Rewriter, Op->getLoc(), Product, F);
    Rewriter.modifyOpInPlace(Op, [&] { Operand.set(Product); });
    Changed = true;
  }
  return llvm::success(Changed);
}

// Partition the blades of a motor or bivector by their role in a rigid
// body motion or return false if a blade is not of even grade or
// contains the origin.
namespace {
struct MotorParts {
  llvm::SmallVector<mlir::Value, 1> Scalars;
  llvm::SmallVector<mlir::Value, 3> Rotations;     // Euclidean bivectors
  llvm::SmallVector<mlir::Value, 3> Translations;  // e ∧ ∞
  llvm::SmallVector<mlir::Value, 1> Quadvectors;   // e ∧ e ∧ e ∧ ∞

  bool init(mlir::PatternRewriter& Rewriter, mlir::Value M) {
    for (mlir::Value V : getBladeValues(Rewriter, M)) {
      auto BT = dyn_cast<BladeType>(V.getType());
      if (!BT)
        return false;
      uint32_t Tag = BT.getCanonicalTag().getTag();
      if (Tag & NoTag)
        return false;
      switch (BT.getGrade()) {
      case 0:
        Scalars.push_back(V);
        break;
      case 2:
        (Tag & NiTag ? Translations : Rotations).push_back(V);
        break;
      case 4:
        Quadvectors.push_back(V);
        break;
      default:
        return false;
      }
    }
    return true;
  }
};
}  // namespace

llvm::LogicalResult ExpandGrade::matchAndRewrite(
    geomalg::GradeOp Op,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Value Arg = Op.getArg();
  if (isUnknown(Arg))
    return llvm::failure();
  if (isZero(Arg))
    return replaceWithZero(Rewriter, Op);

  llvm::SmallVector<mlir::Value, 8> Blades = getBladeValues(Rewriter, Arg);
  llvm::erase_if(Blades, [&](mlir::Value B) {
    auto BT = dyn_cast<BladeType>(B.getType());
    return !BT || BT.getGrade() != Op.getGrade();
  });
  if (Blades.empty())
    return replaceWithZero(Rewriter, Op);
  replaceOp(Rewriter, Op, createSum(Rewriter, Op.getLoc(), Blades));
  return llvm::success();
}

// The threshold of the squared rotation angle below which
// the rotation is treated as the identity.
static constexpr float SmallAngleSq = 0x1p-24f;

// exp(B) = cos φ + sinc φ (B + W) + ((sinc φ - cos φ) / φ²) Bᵣ W
// where B = Bᵣ + Bₜ, φ² = -Bᵣ², and W = Bᵣ ∧ Bₜ.
llvm::LogicalResult ExpandExp::matchAndRewrite(
    geomalg::ExpOp Op,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = Op.getLoc();
  mlir::Value B = Op.getArg();
  if (!Metric || isUnknown(B))
    return llvm::failure();

  ScalarBuilder S(Rewriter, Loc);
  if (isZero(B)) {
    replaceOp(Rewriter, Op, S.constant(1.0f));
    return llvm::success();
  }

  MotorParts Parts;
  if (!Parts.init(Rewriter, B) || !Parts.Scalars.empty() ||
      !Parts.Quadvectors.empty())
    return Rewriter.notifyMatchFailure(Op, "expecting a motor bivector");

  // exp(Bₜ) = 1 + Bₜ
  if (Parts.Rotations.empty()) {
    replaceOpWithNewOp<SumOp>(Rewriter, Op, S.constant(1.0f), B);
    return llvm::success();
  }

  mlir::Value Br = createSum(Rewriter, Loc, Parts.Rotations);
  mlir::Value PhiSq = S.neg(S.mul(Br, Br));
  mlir::Value Phi = S.sqrt(PhiSq);
  mlir::Value Cos = S.cos(Phi);
  mlir::Value Small = S.constant(SmallAngleSq);
  mlir::Value Sinc = S.selectLt(PhiSq, Small, S.constant(1.0f),
                                S.div(S.sin(Phi), Phi));
  mlir::Value Result;
  if (Parts.Translations.empty()) {
    Result = S.add(Cos, S.mul(Sinc, B));
  } else {
    mlir::Value Bt = createSum(Rewriter, Loc, Parts.Translations);
    mlir::Value W = OuterProdOp::create(Rewriter, Loc, Br, Bt);
    mlir::Value Q = S.selectLt(PhiSq, Small, S.constant(0.0f),
                               S.div(S.sub(Sinc, Cos), PhiSq));
    mlir::Value BrW = GeomProdOp::create(Rewriter, Loc, Br, W);
    std::array<mlir::Value, 3> Terms = {Cos,
                                        S.mul(Sinc, S.add(B, W)),
                                        S.mul(Q, BrW)};
    Result = SumOp::create(Rewriter, Loc, Terms);
  }
  replaceOp(Rewriter, Op, Result);
  return llvm::success();
}

// log(M) = σ (φ / r) (Bᵣ + Bₜ) - ((1 - |s| φ / r) / r²) Bᵣ P
// where M = s + Bᵣ + Bₜ + P, r² = -Bᵣ², φ = atan2(r, |s|), and σ = sign(s)
// so the branch with s ≥ 0 is chosen and M need not be normalized.
llvm::LogicalResult ExpandLog::matchAndRewrite(
    geomalg::LogOp Op,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = Op.getLoc();
  mlir::Value M = Op.getArg();
  if (!Metric || isUnknown(M) || isZero(M))
    return llvm::failure();

  MotorParts Parts;
  if (!Parts.init(Rewriter, M))
    return Rewriter.notifyMatchFailure(Op, "expecting a motor");

  ScalarBuilder S(Rewriter, Loc);
  bool HasScalar = !Parts.Scalars.empty();
  mlir::Value Sc = HasScalar ? createSum(Rewriter, Loc, Parts.Scalars)
                             : mlir::Value();

  // log(s + Bₜ) = Bₜ / s
  if (Parts.Rotations.empty()) {
    if (Parts.Translations.empty() || !HasScalar)
      return replaceWithZero(Rewriter, Op);
    mlir::Value Bt = createSum(Rewriter, Loc, Parts.Translations);
    replaceOp(Rewriter, Op, S.mul(S.inv(Sc), Bt));
    return llvm::success();
  }

  mlir::Value Br = createSum(Rewriter, Loc, Parts.Rotations);
  mlir::Value RSq = S.neg(S.mul(Br, Br));
  mlir::Value R = S.sqrt(RSq);
  mlir::Value Zero = S.constant(0.0f);
  mlir::Value AbsS = HasScalar ? S.selectLt(Sc, Zero, S.neg(Sc), Sc) : Zero;
  mlir::Value AngleRatio = S.div(S.atan2(R, AbsS), R);
  mlir::Value BCoeff = AngleRatio;
  mlir::Value PCoeff = HasScalar
    ? S.div(S.sub(S.constant(1.0f), S.mul(AbsS, AngleRatio)), RSq)
    : S.inv(RSq);
  if (HasScalar) {
    // Use the limits as r → 0 for small rotations.
    mlir::Value Small = S.mul(S.constant(SmallAngleSq), S.mul(Sc, Sc));
    mlir::Value Signed = S.selectLt(Sc, Zero, S.neg(AngleRatio), AngleRatio);
    BCoeff = S.selectLt(RSq, Small, S.inv(Sc), Signed);
    PCoeff = S.selectLt(RSq, Small, Zero, PCoeff);
  }

  mlir::Value Biv = Br;
  if (!Parts.Translations.empty())
    Biv = S.add(Br, createSum(Rewriter, Loc, Parts.Translations));
  mlir::Value Result = S.mul(BCoeff, Biv);
  if (!Parts.Quadvectors.empty()) {
    mlir::Value P = createSum(Rewriter, Loc, Parts.Quadvectors);
    mlir::Value BrP = GeomProdOp::create(Rewriter, Loc, Br, P);
    Result = S.sub(Result, S.mul(PCoeff, BrP));
  }
  replaceOp(Rewriter, Op, Result);
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
      replaceOp(Rewriter, LC, LHS);
      return llvm::success();
    }
  }

  // At this point only transform when the result type is not yet known.
  if (!isUnknown(LC.getResult()))
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

  // Inverse of a constant is a constant.
  if (auto B = Arg.getDefiningOp<BladeOp>()) {
    replaceOpWithNewOp<BladeOp>(Rewriter, InvOp, B.getResult().getType(),
                                                 1.0f / B.getFloat());
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
  auto MV = dyn_cast<geomalg::MultivectorLike>(Arg.getType());

  // Scalars are lowered to division in the dialect conversion.
  if ((!BT && !MV) || (BT && BT.getGrade() == 0))
    return llvm::failure();

  // V⁻¹ = Ṽ / ⟨V Ṽ⟩₀ for a multivector V assumed to be a versor.
  if (MV && !MV.isVector()) {
    mlir::Value NormSq = createVersorNormSq(Rewriter, Loc, Arg);
    mlir::Value Rev = geomalg::ReverseOp::create(Rewriter, Loc, Arg);
    replaceOpWithNewOp<geomalg::InnerProdOp>(
        Rewriter, InvOp, geomalg::InverseOp::create(Rewriter, Loc, NormSq),
        Rev);
    return llvm::success();
  }


  // For basis blades we can simplify to a Negate since we know the grade.
  mlir::Value Reverse;
  if (BT) {
    Reverse = BT.shouldReverseNegate()
      ? geomalg::NegateOp::create(Rewriter, Loc, Arg.getType(), Arg)
      : Arg;
  } else {
    Reverse = geomalg::ReverseOp::create(Rewriter, Loc, Arg);
  }

  // Multiply the inverse of norm squared and reverse blade.
  mlir::Value SquareInverse = squareInverse(Rewriter, Metric, Loc, Arg);
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
      if (isOrthogonalBasis(Metric, MVL, MVR)) {
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

// Maybe this is overkill.
llvm::LogicalResult SimplifyDoubling::matchAndRewrite(
    SumOp Op,
    mlir::PatternRewriter& Rewriter) const {
  mlir::Location Loc = Op->getLoc();
  // For any sum with two of the same operand, replace
  // those with a single term that multiplies by 2.
  // We expect a sorted list of operands where duplicates
  // are adjacent.
  if (!isa<BladeType>(Op.getResult().getType()))
    return llvm::failure();
  mlir::ValueRange Operands = Op->getOperands();
  auto Itr = std::adjacent_find(Operands.begin(), Operands.end());
  if (Itr == Operands.end())
    return llvm::failure();

  mlir::ValueRange SubOperands(Itr, Operands.end());
  mlir::Value Term = *Itr;
  auto Count = llvm::count(SubOperands, Term);
  auto EvenCount = (Count / 2) * 2; // Get highest even count.
  mlir::Value CountV = BladeOp::create(Rewriter, Loc,
                                       static_cast<float>(Count));
  mlir::Value Mul = CMulOp::create(Rewriter, Loc, Term.getType(),
                                   {CountV, Term});

  llvm::SmallVector<mlir::Value, 8> NewOperands;
  llvm::append_range(NewOperands, Operands.drop_back(SubOperands.size()));
  NewOperands.push_back(Mul);
  llvm::append_range(NewOperands, SubOperands.drop_front(EvenCount));
  replaceOpWithNewOp<SumOp>(Rewriter, Op, NewOperands);
  return llvm::success();
}

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

  // Expand Dot product of constants. (The user created it probably.)
  // TODO Maybe we should have a ConstantSumOp or something.
  if (auto Sum1 = LHS.getDefiningOp<SumOp>()) {
    if (auto Sum2 = RHS.getDefiningOp<SumOp>()) {
      if (Sum1.getArgs().size() == Sum2.getArgs().size()) {
        mlir::Location Loc = Op->getLoc();
        float Dot = 0.0f;
        // Create a constant... duh!
        for (auto [V1, V2] : llvm::zip(Sum1.getArgs(), Sum2.getArgs())) {
          auto B1 = V1.getDefiningOp<BladeOp>();
          auto B2 = V1.getDefiningOp<BladeOp>();
          if (B1 && B2) {
            Dot += B1.getFloat() * B2.getFloat();
          } else {
            Dot = NAN;
            break;
          }
        }
        if (!std::isnan(Dot)) {
          replaceOpWithNewOp<BladeOp>(Rewriter, Op, Dot);
          return llvm::success();
        }
      }
    }
  }

  return llvm::failure();
}

namespace {
void populateDistributePatterns(mlir::RewritePatternSet& PS) {
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<Distribute>(Ctx, mlir::PatternBenefit(1));
  PS.add<DistributeVP>(Ctx, mlir::PatternBenefit(0));
}

// This is currently unused, but I really liked the idea of it,
// and perhaps it can be used for analysis such as finding inverses
// of arbitrary multivectors.
void populateMatvecPatterns(mlir::RewritePatternSet& PS) {
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<ExpandMatvec>(Ctx, mlir::PatternBenefit(100));
}

void populateExpandPatterns(mlir::RewritePatternSet& PS, MetricKind MK) {
  // Populate all patterns related to expanding the
  // algebra operations except for distribution.
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<ZeroAbsorbToZero,
         SimplifyInverse,
         SimplifyNegate,
         SimplifyDot>(Ctx, mlir::PatternBenefit(10));
  PS.add<ExpandLC,
         ExpandGP,
         ExpandGrade,
         RemoveExpand,
         RemoveCast,
         RemoveConvert,
         ExpandReverse,
         ExpandGradeInvo,
         UpdateCall,
         UpdateInferredTypes,
         ConvertNarrowedReturn,
         ConvertNarrowedCallArgs>(Ctx, mlir::PatternBenefit(5));
  PS.add<ExpandInverse,
         ExpandConvert,
         ExpandExp,
         ExpandLog
           >(Metric::get(MK), Ctx, mlir::PatternBenefit(5));
  PS.add<ExpandVP>(Metric::get(MK), Ctx, mlir::PatternBenefit(2));
  PS.add<ExpandVersorVP,
         ExpandVersorUnary,
         ExpandVersorOperand>(Ctx, mlir::PatternBenefit(10));
  if (MK != MetricKind::unknown) {
    PS.add<ApplyMetric>(geomalg::Metric::get(MK), Ctx,
                        mlir::PatternBenefit(10));
  }
}

void populateExpandSumPatterns(mlir::RewritePatternSet& PS) {
  // Populate all patterns related to expanding the
  // algebra operations except for distribution.
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<ExpandSum>(Ctx, mlir::PatternBenefit(0));
}

// Functions with no Geomalg operations are left untouched
// (e.g. by CSE) since they may belong to another dialect.
static bool hasGeomalgOps(mlir::func::FuncOp FuncOp) {
  return FuncOp.walk([](mlir::Operation* Op) {
      return llvm::isa_and_nonnull<geomalg::GeomalgDialect>(Op->getDialect())
        ? mlir::WalkResult::interrupt()
        : mlir::WalkResult::advance();
    }).wasInterrupted();
}

class ExpandFuncPass
              : public geomalg::impl::ExpandFuncPassBase<ExpandFuncPass> {
  using Base = geomalg::impl::ExpandFuncPassBase<ExpandFuncPass>;
  mlir::FrozenRewritePatternSet ExpandPatterns;
  mlir::FrozenRewritePatternSet ExpandSumPatterns;

public:
  using Base::Base;

  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    {
      mlir::RewritePatternSet PS(Ctx);
      populateExpandPatterns(PS, metric);
      populateDistributePatterns(PS);
      ExpandPatterns = mlir::FrozenRewritePatternSet(std::move(PS),
                              disabledPatterns,
                              enabledPatterns);
    }
    {
      mlir::RewritePatternSet PS(Ctx);
      populateExpandSumPatterns(PS);
      ExpandSumPatterns = mlir::FrozenRewritePatternSet(std::move(PS));
    }
    return llvm::success();
  }

  void runOnOperation() override {
    if (llvm::failed(run(getOperation())))
      signalPassFailure();
  }

  llvm::LogicalResult run(mlir::func::FuncOp FuncOp) const {
    mlir::MLIRContext* Ctx = FuncOp->getContext();

    // A function with no Geomalg operations has nothing to expand.
    if (!hasGeomalgOps(FuncOp))
      return llvm::success();

    bool AnyIRChanged = true;
    while (AnyIRChanged) {
      AnyIRChanged = false;
      bool IRChanged = false;
      if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, ExpandPatterns,
                  mlir::GreedyRewriteConfig(), &IRChanged)))
        return llvm::failure();
      AnyIRChanged |= IRChanged;

      // Run CSE... maybe.
      if (IRChanged) {
        mlir::IRRewriter Rewriter(Ctx);
        mlir::DominanceInfo DI;
        mlir::eliminateCommonSubExpressions(Rewriter, DI, FuncOp, &IRChanged);
      }
      AnyIRChanged |= IRChanged;

      if (llvm::failed(mlir::applyPatternsGreedily(FuncOp, ExpandSumPatterns,
                  mlir::GreedyRewriteConfig(), &IRChanged)))
        return llvm::failure();
      AnyIRChanged |= IRChanged;

      // Run CSE.
      {
        mlir::IRRewriter Rewriter(Ctx);
        mlir::DominanceInfo DI;
        mlir::eliminateCommonSubExpressions(Rewriter, DI, FuncOp, &IRChanged);
      }
      AnyIRChanged |= IRChanged;
    }

    // Check the call args in the body of the function.
    if (llvm::failed(checkCallArgs(FuncOp)))
        return llvm::failure();

    return llvm::success();
  }
};

class SimplifyPass : public geomalg::impl::SimplifyPassBase<SimplifyPass> {
  using Base = geomalg::impl::SimplifyPassBase<SimplifyPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;
  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::func::FuncOp FuncOp = getOperation();
    if (!hasGeomalgOps(FuncOp))
      return;

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

  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    // Create pattern rewriter thingy.
    mlir::RewritePatternSet PS(Ctx);
    PS.add<ZeroAbsorbToZero>(Ctx, mlir::PatternBenefit(10));
    PS.add<SimplifyDoubling>(Ctx, mlir::PatternBenefit(0));
    PS.add<SimplifyMul,
           SimplifySum,
           SimplifyInverse,
           SimplifyNegate,
           SimplifyDot,
           RemoveExpand,
           RemoveConvert,
           ConvertNarrowedReturn,
           ConvertNarrowedCallArgs
           >(Ctx, mlir::PatternBenefit(1));
    // Expand the conversions that widen values narrowed
    // by simplification. (No metric is needed to widen.)
    PS.add<ExpandConvert>(geomalg::Metric::get(MetricKind::unknown), Ctx,
                          mlir::PatternBenefit(1));
    Patterns = mlir::FrozenRewritePatternSet(std::move(PS),
                          disabledPatterns,
                          enabledPatterns);
    return llvm::success();
  }
};

// Expand all the functions in a module while updating
// call operation result types in a loop as well as checking
// call argument types at the end.
class ExpandPass : public geomalg::impl::ExpandPassBase<ExpandPass> {
  using Base = geomalg::impl::ExpandPassBase<ExpandPass>;
  mlir::FrozenRewritePatternSet Patterns;
  geomalg::ExpandFuncPassOptions ExpandPassOpts;

public:
  using Base::Base;

  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    mlir::RewritePatternSet PS(Ctx);
    PS.add<UpdateFuncReturnType>(Ctx);
    Patterns = mlir::FrozenRewritePatternSet(std::move(PS));

    // All just to use the generated options :/
    llvm::SmallVector<std::string>
     disabledPatternsTemp((llvm::ArrayRef<std::string>(disabledPatterns)));
    llvm::SmallVector<std::string>
     enabledPatternsTemp((llvm::ArrayRef<std::string>(enabledPatterns)));
    ExpandPassOpts = geomalg::ExpandFuncPassOptions{
      .metric = metric,
      .disabledPatterns = std::move(disabledPatternsTemp),
      .enabledPatterns = std::move(enabledPatternsTemp)
    };

    return llvm::success();
  }

  void runOnOperation() override {
    mlir::ModuleOp ModuleOp = getOperation();
    if (funcName.empty()) {
      if (llvm::failed(run(ModuleOp)))
        signalPassFailure();
      return;
    }

    auto FuncOp = ModuleOp.lookupSymbol<mlir::func::FuncOp>(funcName);
    if (!FuncOp) {
      ModuleOp->emitError("function not found: ") << funcName;
      return signalPassFailure();
    }
    if (FuncOp.isExternal()) {
      FuncOp->emitError("cannot expand external function");
      return signalPassFailure();
    }
    if (llvm::failed(run(FuncOp)))
      signalPassFailure();
  }

  // Expand a single function updating its result type.
  llvm::LogicalResult run(mlir::func::FuncOp FuncOp) {
    mlir::OpPassManager PM(mlir::func::FuncOp::getOperationName());
    PM.addPass(geomalg::createExpandFuncPass(ExpandPassOpts));
    if (simplify)
      PM.addPass(geomalg::createSimplifyPass());

    if (llvm::failed(runPipeline(PM, FuncOp)))
      return llvm::failure();

    return mlir::applyOpPatternsGreedily(FuncOp.getOperation(), Patterns);
  }

  llvm::LogicalResult run(mlir::ModuleOp ModuleOp) {
    bool IRChanged = true;
    while (IRChanged) {
      // Run ExpandFuncPass with the metric on the nested FuncOps.
      mlir::OpPassManager PM(mlir::ModuleOp::getOperationName());
      PM.addNestedPass<mlir::func::FuncOp>(
          geomalg::createExpandFuncPass(ExpandPassOpts));
      if (simplify)
        PM.addNestedPass<mlir::func::FuncOp>(geomalg::createSimplifyPass());

      if (llvm::failed(runPipeline(PM, ModuleOp)))
        return llvm::failure();

      if (llvm::failed(mlir::applyPatternsGreedily(ModuleOp, Patterns,
                  mlir::GreedyRewriteConfig(), &IRChanged)))
        return llvm::failure();
    }

    return llvm::success();
  }
};

// FIXME The name is too specific to SPIRV if we also use it for LLVM.
// Since 'unknown' is mainly for testing, default to
// 'cga' for the metric.
struct GeomalgToSPIRVOptions
        : public mlir::PassPipelineOptions<GeomalgToSPIRVOptions> {
  Option<geomalg::MetricKind> MetricName{*this, "metric",
    llvm::cl::desc("A metric determines the result of certain operations"),
    ::llvm::cl::init(geomalg::MetricKind::cga), // Default to cga.
    geomalg::getMetricKindEnumValues()};
};
} // namespace

// Do the complete compilation down to SPIRV functions.
// The module will likely have to be created explicitly.
void geomalg::registerGeomalgToSPIRV() {
  auto BuildFn = [](mlir::OpPassManager& PM,
                    GeomalgToSPIRVOptions const& Options) {
    geomalg::ExpandPassOptions EPO{.metric = Options.MetricName};
    PM.addPass(createExpandPass(EPO));
    //PM.addPass(mlir::createSCCPPass());
    PM.addPass(createLowerPass());
    PM.addNestedPass<mlir::func::FuncOp>(mlir::createCanonicalizerPass());
    PM.addNestedPass<mlir::func::FuncOp>(mlir::createCSEPass());
    PM.addPass(createLowerToSPIRVPass());
  };

  mlir::PassPipelineRegistration<GeomalgToSPIRVOptions>(
        "geomalg-to-spirv", "Convert Geomalg to SPIRV (cga)", BuildFn);
}

void geomalg::registerGeomalgToLLVM() {
  auto BuildFn = [](mlir::OpPassManager& PM,
                    GeomalgToSPIRVOptions const& Options) {
    geomalg::ExpandPassOptions EPO{.metric = Options.MetricName};
    PM.addPass(createExpandPass(EPO));
    //PM.addPass(mlir::createSCCPPass());
    PM.addPass(createLowerPass());
    PM.addNestedPass<mlir::func::FuncOp>(mlir::createCanonicalizerPass());
    PM.addNestedPass<mlir::func::FuncOp>(mlir::createCSEPass());
    PM.addPass(createLowerToLLVMPass());
  };

  mlir::PassPipelineRegistration<GeomalgToSPIRVOptions>(
        "geomalg-to-llvm", "Convert Geomalg to LLVM (cga)", BuildFn);
}
