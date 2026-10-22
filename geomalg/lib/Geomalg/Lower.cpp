#include <geomalg/Dialect.h>
#include <geomalg/Metric.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <mlir/Conversion/ArithToSPIRV/ArithToSPIRV.h>
#include <mlir/Conversion/ArithToLLVM/ArithToLLVM.h>
#include <mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h>
#include <mlir/Conversion/FuncToSPIRV/FuncToSPIRV.h>
#include <mlir/Conversion/MathToLLVM/MathToLLVM.h>
#include <mlir/Conversion/MathToSPIRV/MathToSPIRV.h>
#include <mlir/Conversion/LLVMCommon/ConversionTarget.h>
#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Conversion/VectorToSPIRV/VectorToSPIRV.h>
#include <mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/Transforms/FuncConversions.h>
#include <mlir/Dialect/Linalg/IR/Linalg.h>
#include <mlir/Dialect/Math/IR/Math.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/SPIRV/IR/TargetAndABI.h>
#include <mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/Passes.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/CommandLine.h>

#ifndef NDEBUG
#include <mlir/IR/Verifier.h>
#endif // NDEBUG

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_LOWERPASS
#define GEN_PASS_DEF_LOWERTOSPIRVPASS
#define GEN_PASS_DEF_LOWERTOLLVMPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace arith = mlir::arith;
namespace func = mlir::func;
namespace linalg = mlir::linalg;
namespace math = mlir::math;
namespace spirv = mlir::spirv;
namespace tensor = mlir::tensor;
namespace vector = mlir::vector;
using namespace geomalg;

namespace {
mlir::ValueRange expandVector(mlir::RewriterBase& R, mlir::Location Loc,
                              mlir::Value InputVector) {
  // If the vector is defined "from_elements" then just use the operands
  if (auto DefOp = InputVector.getDefiningOp<vector::FromElementsOp>())
    return DefOp.getElements();
  auto VT = dyn_cast<mlir::VectorType>(InputVector.getType());
  assert(VT && VT.getRank() == 1 &&
      "expecting a 1-d vector");
  assert(R.getContext()->getLoadedDialect<vector::VectorDialect>());
  // Extract the elements once right after the definition of the vector.
  mlir::Block* DefBlock = InputVector.getParentBlock();
  for (mlir::Operation* User : InputVector.getUsers()) {
    auto ToElements = dyn_cast<vector::ToElementsOp>(User);
    if (ToElements && ToElements->getBlock() == DefBlock)
      return ToElements.getResults();
  }
  mlir::OpBuilder::InsertionGuard Guard(R);
  R.setInsertionPointAfterValue(InputVector);
  auto NewOp = vector::ToElementsOp::create(R, Loc, InputVector);
  return NewOp.getResults();
}

// All types will be converted a single scalar type
// and vectors of that scalar type.
// Types from other dialects are not converted.
struct TypeConverter : mlir::TypeConverter {
  TypeConverter(mlir::Type ScalarT)
    : mlir::TypeConverter()
  {
    // Geomalg types with no conversion (e.g. unknown) fail.
    addConversion([](mlir::Type T) -> mlir::Type {
      if (llvm::isa<GeomalgDialect>(T.getDialect()))
        return mlir::Type();
      return T;
    });
    addConversion([ScalarT](BladeType BT) { return ScalarT; });
    addConversion([ScalarT](ZeroType ZT) { return ScalarT; });
    addConversion([ScalarT](MultivectorLike MV) {
      int64_t Size = MV.getBlades().size();
      return mlir::VectorType::get(Size, ScalarT);
    });
    // A versor is stored as the concatenated elements of its factors.
    addConversion([ScalarT](VersorType VT) {
      int64_t Size = 0;
      for (mlir::Type F : VT.getFactors()) {
        auto MV = dyn_cast<MultivectorLike>(F);
        Size += MV ? MV.getBlades().size() : 1;
      }
      return mlir::VectorType::get(Size, ScalarT);
    });
  }
};

struct ConversionTarget : mlir::ConversionTarget {
  ConversionTarget(mlir::MLIRContext& Ctx, mlir::TypeConverter const& TC)
    : mlir::ConversionTarget(Ctx)
  {
    // Legalize stuff.
    addLegalDialect<arith::ArithDialect>();
    addLegalDialect<linalg::LinalgDialect>();
    addLegalDialect<math::MathDialect>();
    addLegalDialect<func::FuncDialect>();
    addLegalDialect<vector::VectorDialect>();
    // Function signatures, calls, and returns with geomalg types
    // are converted.
    addDynamicallyLegalOp<func::FuncOp>([&TC](func::FuncOp Op) {
      return TC.isSignatureLegal(Op.getFunctionType()) &&
             TC.isLegal(&Op.getBody());
    });
    addDynamicallyLegalOp<func::CallOp, func::ReturnOp>(
      [&TC](mlir::Operation* Op) { return TC.isLegal(Op); });
  }
};

class PatternBase {
public:
  template <typename Self>
  mlir::Type getScalarT(this Self& self) {
    mlir::MLIRContext* Ctx = self.getContext();
    mlir::TypeConverter const* TC = self.getTypeConverter();
    mlir::Type BT = BladeType::get(Ctx, 0);
    return TC->convertType(BT);
  }
};

// Conversion Patterns

struct CMulToMul : mlir::OpConversionPattern<CMulOp>,
                   ::PatternBase {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        CMulOp Op, CMulOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::ValueRange Args = Adaptor.getArgs();
    mlir::Type ScalarT = getScalarT();

    if (Args.empty()) {
      // Return a constant 1.0f.
      R.replaceOpWithNewOp<arith::ConstantOp>(Op, R.getOneAttr(ScalarT));
    } else {
      mlir::Value NewResult = Args.front();
      for (mlir::Value Arg : Args.drop_front())
        NewResult = arith::MulFOp::create(R, Op.getLoc(), ScalarT,
                                          NewResult, Arg);
      R.replaceOp(Op, NewResult);
    }
    return llvm::success();
  }
};

struct SumToTensor : mlir::OpConversionPattern<SumOp>,
                  ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        SumOp Op, SumOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ScalarT = getScalarT();
    mlir::ValueRange Args = Adaptor.getArgs();
    mlir::Value Result = Op.getResult();
    mlir::Type ResultT = Result.getType();
    if (Args.empty()) {
      // Return a constant 0.0f.
      R.replaceOpWithNewOp<arith::ConstantOp>(Op, R.getZeroAttr(ScalarT));
    } else if (auto MV = dyn_cast<MultivectorLike>(ResultT)) {
      // Create a tensor.
      size_t Size = MV.getBlades().size();
      mlir::Type RT = mlir::RankedTensorType::get(Size, ScalarT);
      R.replaceOpWithNewOp<tensor::FromElementsOp>(Op, RT, Args);
    } else {
      assert(isa<BladeType>(ResultT));
      // SumOp type inference guarantees like terms here so
      // this is where we can actually perform addition.
      mlir::Value NewResult = Args.front();
      for (mlir::Value Arg : Args.drop_front())
        NewResult = arith::AddFOp::create(R, Op.getLoc(), NewResult, Arg);
      R.replaceOp(Op, NewResult);
    }
    return llvm::success();
  }
};

struct SumToVector : mlir::OpConversionPattern<SumOp>,
                     ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        SumOp Op, SumOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ScalarT = getScalarT();
    mlir::ValueRange Args = Adaptor.getArgs();
    mlir::Value Result = Op.getResult();
    mlir::Type ResultT = Result.getType();
    if (Args.empty()) {
      // Return a constant 0.0f.
      R.replaceOpWithNewOp<arith::ConstantOp>(Op, R.getZeroAttr(ScalarT));
    } else if (auto MV = dyn_cast<MultivectorLike>(ResultT)) {
      // Create a vector.
      int64_t Size = MV.getBlades().size();
      mlir::Type RT = mlir::VectorType::get({Size}, ScalarT);
      R.replaceOpWithNewOp<vector::FromElementsOp>(Op, RT, Args);
    } else {
      if (!isa<BladeType>(ResultT))
        return R.notifyMatchFailure(Op, "expecting blade type for sum operand");
      // SumOp type inference guarantees like terms here so
      // this is where we can actually perform addition.
      mlir::Value NewResult = Args.front();
      for (mlir::Value Arg : Args.drop_front())
        NewResult = arith::AddFOp::create(R, Op.getLoc(), NewResult, Arg);
      R.replaceOp(Op, NewResult);
    }
    return llvm::success();
  }
};

struct ExpandToVector : mlir::OpConversionPattern<ExpandOp>,
                        ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        ExpandOp Op, ExpandOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    // ExpandOp always "extracts" every element.
    mlir::Location Loc = Op->getLoc();
    mlir::Value InputVector = Adaptor.getArg();
    mlir::ValueRange NewResults = expandVector(R, Loc, InputVector);
    R.replaceOp(Op, NewResults);
    return llvm::success();
  }
};

struct VersorToVector : mlir::OpConversionPattern<VersorOp>,
                        ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        VersorOp Op, VersorOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    auto ResultT = dyn_cast_if_present<mlir::VectorType>(
        getTypeConverter()->convertType(Op.getResult().getType()));
    if (!ResultT)
      return R.notifyMatchFailure(Op, "expecting versor result type");

    // Concatenate the elements of the factors.
    llvm::SmallVector<mlir::Value, 8> Elements;
    for (mlir::Value F : Adaptor.getFactors()) {
      if (isa<mlir::VectorType>(F.getType()))
        llvm::append_range(Elements, expandVector(R, Loc, F));
      else
        Elements.push_back(F);
    }
    if (static_cast<int64_t>(Elements.size()) != ResultT.getNumElements())
      return R.notifyMatchFailure(Op, "factors do not match versor type");

    R.replaceOpWithNewOp<vector::FromElementsOp>(Op, ResultT, Elements);
    return llvm::success();
  }
};

struct FactorsToVector : mlir::OpConversionPattern<FactorsOp>,
                         ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        FactorsOp Op, FactorsOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::ValueRange Elements = expandVector(R, Loc, Adaptor.getArg());

    // Split the elements into the factors.
    llvm::SmallVector<mlir::Value, 4> NewResults;
    for (mlir::Type T : Op.getResultTypes()) {
      mlir::Type FT = getTypeConverter()->convertType(T);
      if (auto VT = dyn_cast_if_present<mlir::VectorType>(FT)) {
        int64_t Size = VT.getNumElements();
        NewResults.push_back(vector::FromElementsOp::create(R, Loc, VT,
                               Elements.take_front(Size)));
        Elements = Elements.drop_front(Size);
      } else if (FT) {
        NewResults.push_back(Elements.front());
        Elements = Elements.drop_front();
      } else {
        return R.notifyMatchFailure(Op, "expecting factor type");
      }
    }

    R.replaceOp(Op, NewResults);
    return llvm::success();
  }
};

struct LowerBlade : mlir::OpConversionPattern<BladeOp>,
                    ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        BladeOp Op, BladeOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ScalarT = getScalarT();
    mlir::FloatAttr C = Op.getCoefficientAttr();
    R.replaceOpWithNewOp<arith::ConstantOp>(Op, ScalarT, C);
    return llvm::success();
  }
};

struct LowerNegate : mlir::OpConversionPattern<NegateOp>,
                     ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        NegateOp Op, NegateOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ScalarT = getScalarT();
    mlir::Value Arg = Adaptor.getArg();
    R.replaceOpWithNewOp<arith::NegFOp>(Op, Arg);
    return llvm::success();
  }
};

struct DotToLinalg : mlir::OpConversionPattern<DotOp>,
                  ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        DotOp Op, DotOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();
    mlir::ValueRange Inputs = Adaptor.getOperands();
    // The linalg::DotOp "returns" a tensor<f32> so we have to unwrap it.
    mlir::Value
      Zero = arith::ConstantOp::create(R, Loc, R.getZeroAttr(ScalarT));
    mlir::Type TensorT = mlir::RankedTensorType::get({}, ScalarT);
    mlir::Value Output = tensor::EmptyOp::create(R, Loc, TensorT,
                                                 mlir::ValueRange{});
    auto FillOp = linalg::FillOp::create(R, Loc, Zero, Output);
    Output = FillOp.getResults().front();
    linalg::DotOp Dot = linalg::DotOp::create(R, Loc, Inputs, Output);
    mlir::Value DotResult = Dot.getResults().front();
    R.replaceOpWithNewOp<tensor::ExtractOp>(Op, ScalarT, DotResult);
    return llvm::success();
  }
};

struct DotToArith : mlir::OpConversionPattern<DotOp>,
                    ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        DotOp Op, DotOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();
    mlir::Value LHSVector = Adaptor.getLHS();
    mlir::Value RHSVector = Adaptor.getRHS();
    mlir::ValueRange LHSs = expandVector(R, Loc, LHSVector);
    mlir::ValueRange RHSs = expandVector(R, Loc, RHSVector);
    assert(LHSs.size() == RHSs.size() && LHSs.size() != 0);
    llvm::SmallVector<mlir::Value, 8> MulResults;
    for (auto [LHS, RHS] : llvm::zip(LHSs, RHSs))
      MulResults.push_back(arith::MulFOp::create(R, Loc, LHS, RHS));
    mlir::ValueRange MR = mlir::ValueRange(MulResults);
    mlir::Value NewResult = MR.front();
    for (mlir::Value V : MR.drop_front())
      NewResult = arith::AddFOp::create(R, Loc, NewResult, V);
    R.replaceOp(Op, NewResult);
    return llvm::success();
  }
};

struct InverseToArith : mlir::OpConversionPattern<InverseOp>,
                        ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        InverseOp Op, InverseOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();
    mlir::Value One = arith::ConstantOp::create(R, Loc, R.getOneAttr(ScalarT));
    R.replaceOpWithNewOp<arith::DivFOp>(Op, One, Adaptor.getArg());
    return llvm::success();
  }
};

// Lower a scalar function to the corresponding math operation.
template <typename OpTy, typename MathOpTy>
struct ScalarToMath : mlir::OpConversionPattern<OpTy>,
                      ::PatternBase {
public:
  using mlir::OpConversionPattern<OpTy>::OpConversionPattern;

  llvm::LogicalResult matchAndRewrite(
        OpTy Op, typename OpTy::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.replaceOpWithNewOp<MathOpTy>(Op, getScalarT(), Adaptor.getOperands());
    return llvm::success();
  }
};

struct SelectLtToArith : mlir::OpConversionPattern<SelectLtOp>,
                         ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        SelectLtOp Op, SelectLtOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Value Cond = arith::CmpFOp::create(R, Op.getLoc(),
                                             arith::CmpFPredicate::OLT,
                                             Adaptor.getLhs(),
                                             Adaptor.getRhs());
    R.replaceOpWithNewOp<arith::SelectOp>(Op, Cond, Adaptor.getTrueValue(),
                                          Adaptor.getFalseValue());
    return llvm::success();
  }
};

// Build f32 approximations of sin, cos, and atan2 with operations that are
// correctly rounded under IEEE 754 (+, -, *, /, negation, comparison and
// select) so the results are the same on every platform. Nothing here may
// call libm, use integer conversions of possibly NaN values, or rely on FMA
// contraction (no fast-math flags are set). The polynomials are from Cephes
// (sinf.c, cosf.c, atanf.c) with errors within a few ulp for finite inputs.
struct PolyBuilder {
  mlir::OpBuilder& B;
  mlir::Location Loc;
  mlir::Type ScalarT;

  mlir::Value constant(float C) {
    return arith::ConstantOp::create(B, Loc, B.getFloatAttr(ScalarT, C));
  }
  mlir::Value add(mlir::Value X, mlir::Value Y) {
    return arith::AddFOp::create(B, Loc, X, Y);
  }
  mlir::Value sub(mlir::Value X, mlir::Value Y) {
    return arith::SubFOp::create(B, Loc, X, Y);
  }
  mlir::Value mul(mlir::Value X, mlir::Value Y) {
    return arith::MulFOp::create(B, Loc, X, Y);
  }
  mlir::Value div(mlir::Value X, mlir::Value Y) {
    return arith::DivFOp::create(B, Loc, X, Y);
  }
  mlir::Value neg(mlir::Value X) {
    return arith::NegFOp::create(B, Loc, X);
  }
  mlir::Value cmp(arith::CmpFPredicate P, mlir::Value X, mlir::Value Y) {
    return arith::CmpFOp::create(B, Loc, P, X, Y);
  }
  mlir::Value lt(mlir::Value X, mlir::Value Y) {
    return cmp(arith::CmpFPredicate::OLT, X, Y);
  }
  mlir::Value eq(mlir::Value X, float C) {
    return cmp(arith::CmpFPredicate::OEQ, X, constant(C));
  }
  mlir::Value select(mlir::Value Cond, mlir::Value T, mlir::Value F) {
    return arith::SelectOp::create(B, Loc, Cond, T, F);
  }
  mlir::Value abs(mlir::Value X) {
    return math::AbsFOp::create(B, Loc, X);
  }
  mlir::Value copySign(mlir::Value X, mlir::Value Sign) {
    return math::CopySignOp::create(B, Loc, X, Sign);
  }
  // Round to the nearest integer (ties to even) for |X| < 2²²
  // by adding and subtracting 1.5·2²³.
  mlir::Value round(mlir::Value X) {
    mlir::Value Magic = constant(0x1.8p23f);
    return sub(add(X, Magic), Magic);
  }
  // c₀ zⁿ + c₁ zⁿ⁻¹ + ⋯ + cₙ by Horner's method.
  mlir::Value poly(mlir::Value Z, llvm::ArrayRef<float> Coeffs) {
    mlir::Value Result = constant(Coeffs.front());
    for (float C : Coeffs.drop_front())
      Result = add(mul(Result, Z), constant(C));
    return Result;
  }

  // Reduce X to R = X - kπ/2 with |R| ≤ π/4 and return R and the quadrant
  // k mod 4 as a float in {0, 1, 2, 3}. The subtraction of kπ/2 in three
  // parts (Cody-Waite) is exact in the first two for |k| < 2¹².
  std::pair<mlir::Value, mlir::Value> reduceHalfPi(mlir::Value X) {
    mlir::Value K = round(mul(X, constant(0x1.45f306p-1f)));  // 2/π
    mlir::Value R = sub(X, mul(K, constant(0x1.92p0f)));
    R = sub(R, mul(K, constant(0x1.fb4p-12f)));
    R = sub(R, mul(K, constant(0x1.4442d2p-24f)));
    // k - 4 round(k/4) ∈ {-2, -1, 0, 1, 2} is exact.
    mlir::Value M = sub(K, mul(constant(4.0f), round(mul(K, constant(0.25f)))));
    mlir::Value Q = select(lt(M, constant(0.0f)),
                           add(M, constant(4.0f)), M);
    return {R, Q};
  }
  // sin R for |R| ≤ π/4 (with the sign of R so sin(-0) = -0)
  mlir::Value sinPoly(mlir::Value R, mlir::Value Z) {
    mlir::Value P = poly(Z, {-1.9515295891e-4f, 8.3321608736e-3f,
                             -1.6666654611e-1f});
    return copySign(add(mul(mul(P, Z), R), R), R);
  }
  // cos R for |R| ≤ π/4
  mlir::Value cosPoly(mlir::Value Z) {
    mlir::Value P = poly(Z, {2.443315711809948e-5f, -1.388731625493765e-3f,
                             4.166664568298827e-2f});
    mlir::Value Y = sub(mul(mul(P, Z), Z), mul(constant(0.5f), Z));
    return add(Y, constant(1.0f));
  }

  mlir::Value sinOrCos(mlir::Value X, bool IsCos) {
    auto [R, Q] = reduceHalfPi(X);
    mlir::Value Z = mul(R, R);
    mlir::Value S = sinPoly(R, Z);
    mlir::Value C = cosPoly(Z);
    mlir::Value Odd = arith::OrIOp::create(B, Loc, eq(Q, 1.0f), eq(Q, 3.0f));
    // sin(X) = sin R, cos R, -sin R, -cos R and
    // cos(X) = cos R, -sin R, -cos R, sin R for the quadrants 0 to 3.
    mlir::Value Negate = IsCos
      ? arith::OrIOp::create(B, Loc, eq(Q, 1.0f), eq(Q, 2.0f))
      : cmp(arith::CmpFPredicate::OGE, Q, constant(2.0f));
    mlir::Value Y = IsCos ? select(Odd, S, C) : select(Odd, C, S);
    return select(Negate, neg(Y), Y);
  }

  mlir::Value atan2(mlir::Value Y, mlir::Value X) {
    mlir::Value AX = abs(X);
    mlir::Value AY = abs(Y);
    // Take the angle in [0, π/4] of (max(|x|, |y|), min(|x|, |y|)).
    mlir::Value Swap = lt(AX, AY);
    mlir::Value Max = select(Swap, AY, AX);
    mlir::Value Min = select(Swap, AX, AY);
    // Above tan(π/8) use atan(a) = π/4 + atan((a - 1)/(a + 1)).
    mlir::Value Big = lt(mul(Max, constant(0x1.a8279ap-2f)), Min);
    mlir::Value Num = select(Big, sub(Min, Max), Min);
    mlir::Value Den = select(Big, add(Min, Max), Max);
    // atan2(±0, ±0) uses the ratio 0/1.
    Den = select(eq(Den, 0.0f), constant(1.0f), Den);
    mlir::Value T = div(Num, Den);
    mlir::Value Z = mul(T, T);
    mlir::Value P = poly(Z, {8.05374449538e-2f, -1.38776856032e-1f,
                             1.99777106478e-1f, -3.33329491539e-1f});
    mlir::Value Theta = add(mul(mul(P, Z), T), T);
    // The angle is kπ/4 ± θ for k ∈ {0, 1, 2, 3, 4}.
    mlir::Value K = select(Big, constant(1.0f), constant(0.0f));
    K = select(Swap, sub(constant(2.0f), K), K);
    Theta = select(Swap, neg(Theta), Theta);
    // Reflect into the left half plane (including x = -0).
    mlir::Value XNeg = lt(copySign(constant(1.0f), X), constant(0.0f));
    K = select(XNeg, sub(constant(4.0f), K), K);
    Theta = select(XNeg, neg(Theta), Theta);
    // Add kπ/4 in two parts where the 21 bits of Hi make k Hi exact.
    mlir::Value Hi = constant(0x1.921fbp-1f);
    mlir::Value Lo = constant(0x1.5110b4p-23f);
    Theta = add(mul(K, Hi), add(mul(K, Lo), Theta));
    return copySign(Theta, Y);
  }
};

template <typename OpTy>
struct ScalarToPoly : mlir::OpConversionPattern<OpTy>,
                      ::PatternBase {
public:
  using mlir::OpConversionPattern<OpTy>::OpConversionPattern;

  llvm::LogicalResult matchAndRewrite(
        OpTy Op, typename OpTy::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    PolyBuilder P{R, Op.getLoc(), getScalarT()};
    mlir::Value Result;
    if constexpr (std::is_same_v<OpTy, SinOp>)
      Result = P.sinOrCos(Adaptor.getArg(), /*IsCos=*/false);
    else if constexpr (std::is_same_v<OpTy, CosOp>)
      Result = P.sinOrCos(Adaptor.getArg(), /*IsCos=*/true);
    else
      Result = P.atan2(Adaptor.getY(), Adaptor.getX());
    R.replaceOp(Op, Result);
    return llvm::success();
  }
};

struct LowerOSwap : mlir::OpConversionPattern<OSwapOp>,
                    ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        OSwapOp Op, OSwapOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.replaceOpWithNewOp<arith::NegFOp>(Op, Adaptor.getArg());
    return llvm::success();
  }
};

struct LowerCast : mlir::OpConversionPattern<CastOp>,
                        ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        CastOp Op, CastOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    // Remove casts that map to the same (converted) type.
    mlir::Type ResultT = Op.getResult().getType();
    mlir::Type ConvertedResultT = getTypeConverter()->convertType(ResultT);
    if (Adaptor.getArg().getType() == ConvertedResultT) {
      R.replaceOp(Op, Adaptor.getArg());
      return llvm::success();
    }

    return llvm::failure();
  }
};

struct LowerFuncReturn : mlir::OpConversionPattern<ReturnOp>,
                         ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        ReturnOp Op, ReturnOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();
    mlir::Value Arg = Adaptor.getArg();
    mlir::Type OrigT = Op.getArg().getType();
    mlir::Operation* ParentOp = Arg.getParentRegion()->getParentOp();
    auto FuncOp = dyn_cast<mlir::func::FuncOp>(ParentOp);
    if (!FuncOp)
      return llvm::failure();

    mlir::Type FuncResultT = FuncOp.getResultTypes().front();

    // I guess we have to wait for the function signature pass updater thingy.
    if (FuncResultT == Arg.getType())
      return llvm::failure();

    // Replace with the proper ReturnOp for FuncOps.
    R.replaceOpWithNewOp<mlir::func::ReturnOp>(Op, Arg);
    return llvm::success();
  }
};

struct MatvecToLinalg : mlir::OpConversionPattern<MatvecOp>,
                     ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        MatvecOp Op, MatvecOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();
    mlir::RegionRange Regions = Op.getRegions();
    llvm::SmallVector<mlir::Value, 8> Columns;
    using BlockItrTy = mlir::Block::iterator;
    mlir::Block* StartBlock = Op->getBlock();
    mlir::Block* SplitBlock = R.splitBlock(StartBlock, BlockItrTy(Op));
    // Inline each region and keep things as a single block.
    for (mlir::Region* Region : Regions) {
      auto RetOp = cast<ReturnOp>(Region->front().getTerminator());
      mlir::Value Column = R.getRemappedValue(RetOp.getArg());
      if (!Column)
        return R.notifyMatchFailure(Op, "failed to remap value");
      Columns.push_back(Column);
      R.eraseOp(RetOp);
      // MatvecOp regions only ever have a single block.
      R.mergeBlocks(&Region->front(), StartBlock);
    }
    R.mergeBlocks(SplitBlock, StartBlock);
    mlir::Value Vec = Adaptor.getArg();

    // Create the OutputVec.
    int64_t NumRows = Op.getRowDimSize();
    mlir::Type OuputVecT = mlir::RankedTensorType::get({NumRows}, ScalarT);
    mlir::Value OutputVec = tensor::EmptyOp::create(R, Loc, OuputVecT,
                                                    mlir::ValueRange{});
    mlir::Value Zero = arith::ConstantOp::create(R, Loc,
                                                 R.getZeroAttr(ScalarT));
    auto FillOp = linalg::FillOp::create(R, Loc, Zero, OutputVec);
    OutputVec = FillOp.getResults().front();

    // If Columns are a scalar then this should become a single dot product.
    mlir::Value NewResult;
    if (Columns.front().getType() == ScalarT) {
      mlir::Value Vec2 = vector::FromElementsOp::create(R, Loc, ScalarT,
                                                       Columns);
      linalg::DotOp NewOp = linalg::DotOp::create(R, Loc,
            mlir::ValueRange({Vec2, Vec}), OutputVec);
      // Extract the scalar.
      mlir::IntegerAttr IndexAttr = R.getIndexAttr(0);
      mlir::Value Index = arith::ConstantOp::create(R, Loc, IndexAttr);
      NewResult = tensor::ExtractOp::create(R, Loc, NewResult, Index);
    } else {
      mlir::Value Matrix = tensor::ConcatOp::create(R, Loc, /*Dim=*/1, Columns);
      linalg::MatvecOp NewOp = linalg::MatvecOp::create(R, Loc,
            mlir::ValueRange({Matrix, Vec}), OutputVec);
      NewResult = NewOp.getResults().front();
    }

    R.replaceOp(Op, NewResult);
    return llvm::success();
  }
};

// FIXME Lowering geomalg.return should not be a part of the lowering pass.
struct LowerReturn : mlir::OpConversionPattern<ReturnOp>,
                     ::PatternBase {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        ReturnOp Op, ReturnOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    if (!isa<mlir::func::FuncOp>(Op->getParentOp()))
      return llvm::failure();

    // The function signature is converted by the
    // function type conversion pattern.
    R.replaceOpWithNewOp<mlir::func::ReturnOp>(Op, Adaptor.getArg());
    return llvm::success();
  }
};

struct LowerCall : mlir::OpConversionPattern<CallOp>,
                   ::PatternBase {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        CallOp Op, CallOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    auto ModuleOp = Op->getParentOfType<mlir::ModuleOp>();
    mlir::FlatSymbolRefAttr CalleeSym = Op.getCalleeAttr();
    mlir::Operation* LookupOp = ModuleOp.lookupSymbol(CalleeSym.getAttr());
    auto FuncOp = dyn_cast_or_null<mlir::func::FuncOp>(LookupOp);
    if (!FuncOp)
      return llvm::failure();

    mlir::Type ResultT = Op.getResult().getType();

    if (geomalg::isUnknown(ResultT)) {
      Op->emitError("function should have deduced return type before lowering");
      return llvm::failure();
    }

    mlir::Type ConvertedResultT = getTypeConverter()->convertType(ResultT);
    assert(ConvertedResultT && "expecting a converted return type");

    // Replace with new func::CallOp.
    R.replaceOpWithNewOp<func::CallOp>(Op, Op.getCalleeAttr(), ConvertedResultT,
                                       Adaptor.getOperands());
    return llvm::success();
  }
};

void populateLowerPasses(mlir::RewritePatternSet& PS,
                         ::TypeConverter const* TC, bool Deterministic) {
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<CMulToMul,
         SumToVector,
         ExpandToVector,
         VersorToVector,
         FactorsToVector,
         LowerNegate,
         LowerOSwap,
         LowerBlade,
         DotToArith,
         InverseToArith,
         ScalarToMath<SqrtOp, math::SqrtOp>,
         SelectLtToArith,
         LowerCast,
         LowerReturn,
         LowerCall
         >(*TC, Ctx);
  if (Deterministic)
    PS.add<ScalarToPoly<SinOp>,
           ScalarToPoly<CosOp>,
           ScalarToPoly<Atan2Op>
           >(*TC, Ctx);
  else
    PS.add<ScalarToMath<SinOp, math::SinOp>,
           ScalarToMath<CosOp, math::CosOp>,
           ScalarToMath<Atan2Op, math::Atan2Op>
           >(*TC, Ctx);
  mlir::populateFunctionOpInterfaceTypeConversionPattern<func::FuncOp>(PS,
                                                                      *TC);
  mlir::populateCallOpTypeConversionPattern(PS, *TC);
  mlir::populateReturnOpTypeConversionPattern(PS, *TC);
}

llvm::LogicalResult
applyLowerPatterns(mlir::MLIRContext* Ctx, mlir::ModuleOp Op,
                   bool Deterministic) {
  mlir::RewritePatternSet PS(Ctx);
  mlir::Type ScalarT = mlir::Float32Type::get(Ctx);
  // Note that TypeConverter has its pointer captured.
  ::TypeConverter TC(ScalarT);
  populateLowerPasses(PS, &TC, Deterministic);

  // Do that actual Conversion.
  ::ConversionTarget Target(*Ctx, TC);

  mlir::ConversionConfig Config;
  Config.allowPatternRollback = false;
  return mlir::applyPartialConversion(Op, Target, std::move(PS), Config);
}

// Lowering Passes

class LowerPass : public geomalg::impl::LowerPassBase<LowerPass> {
  using Base = geomalg::impl::LowerPassBase<LowerPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();

    mlir::OpPassManager PM(mlir::ModuleOp::getOperationName());
    PM.addNestedPass<func::FuncOp>(geomalg::createFactorPass());
    if (llvm::failed(runPipeline(PM, M)))
      return signalPassFailure();

    llvm::LogicalResult Result = applyLowerPatterns(Ctx, M, deterministic);
    if (llvm::failed(Result))
      signalPassFailure();
  }
};

// This pass is to be run on the lower dialects (ie after geomalg-lower.)
class LowerToSPIRVPass : public geomalg::impl::LowerToSPIRVPassBase<LowerToSPIRVPass> {
  using Base = geomalg::impl::LowerToSPIRVPassBase<LowerToSPIRVPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();

    spirv::TargetEnvAttr TEA = spirv::lookupTargetEnvOrDefault(M);
    mlir::SPIRVTypeConverter STC = mlir::SPIRVTypeConverter(TEA);
    std::unique_ptr<mlir::SPIRVConversionTarget>
      Target = mlir::SPIRVConversionTarget::get(TEA);

    mlir::RewritePatternSet PS(Ctx);
    // SPIRV conversion patterns
    arith::populateArithToSPIRVPatterns(STC, PS);
    mlir::populateFuncToSPIRVPatterns(STC, PS);
    mlir::populateBuiltinFuncToSPIRVPatterns(STC, PS);
    mlir::populateVectorToSPIRVPatterns(STC, PS);
    mlir::populateMathToSPIRVPatterns(STC, PS);

    mlir::LogicalResult
      Result = mlir::applyPartialConversion(M, *Target, std::move(PS));
    if (llvm::failed(Result))
      signalPassFailure();
  }
};

// This pass is to be run on the lower dialects (ie after geomalg-lower.)
class LowerToLLVMPass : public geomalg::impl::LowerToLLVMPassBase<LowerToLLVMPass> {
  using Base = geomalg::impl::LowerToLLVMPassBase<LowerToLLVMPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();

    mlir::LLVMTypeConverter TC = mlir::LLVMTypeConverter(Ctx);
    mlir::LLVMConversionTarget Target(*Ctx);

    mlir::RewritePatternSet PS(Ctx);
    // LLVM conversion patterns
    arith::populateArithToLLVMConversionPatterns(TC, PS);
    mlir::populateFuncToLLVMConversionPatterns(TC, PS);
    mlir::populateVectorToLLVMConversionPatterns(TC, PS);
    mlir::populateMathToLLVMConversionPatterns(TC, PS);

    mlir::LogicalResult
      Result = mlir::applyPartialConversion(M, Target, std::move(PS));
    if (llvm::failed(Result))
      signalPassFailure();
  }
};
}  // namespace

