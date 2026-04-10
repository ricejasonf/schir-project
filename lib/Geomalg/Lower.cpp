#include <geomalg/Dialect.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Tensor/IR/Tensor.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>

// Generated stuff
namespace geomalg {
#define GEN_PASS_DEF_LOWERPASS
#include "geomalg/GeomalgPasses.h.inc"
}

namespace arith = mlir::arith;
namespace tensor = mlir::tensor;
using namespace geomalg;

namespace {
// All types will be converted a single scalar type
// and tensors over said scalar type.
class TypeConverter : public mlir::TypeConverter {
  mlir::Type ScalarT;
public:
  TypeConverter(mlir::Type ScalarT)
    : mlir::TypeConverter(),
      ScalarT(ScalarT)
  {
    addConversion([ScalarT](BladeType BT) { return ScalarT; });
    // addConversion([](MultivectorLike MV) { return ???; });
  }
};

struct ConversionTarget : mlir::ConversionTarget {
  ConversionTarget(mlir::MLIRContext& Ctx)
    : mlir::ConversionTarget(Ctx)
  {
    // Legalize stuff.
    addLegalDialect<arith::ArithDialect>();
    addLegalDialect<tensor::TensorDialect>();
    //addIllegalDialect<geomalg::GeomalgDialect>();
    addIllegalOp<CMulOp>();
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

struct LowerSum : mlir::OpConversionPattern<SumOp>,
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

// Lowering Passes

class LowerPass : public geomalg::impl::LowerPassBase<LowerPass> {
  using Base = geomalg::impl::LowerPassBase<LowerPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();
    ConversionTarget Target(getContext());

    mlir::RewritePatternSet PS(Ctx);
    mlir::Type ScalarT = mlir::Float32Type::get(Ctx);
    // Note that TypeConverter has its pointer captured.
    ::TypeConverter TC(ScalarT);
    PS.add<CMulToMul,
           LowerSum,
           LowerNegate,
           LowerBlade>(TC, Ctx);
    mlir::LogicalResult
      Result = mlir::applyPartialConversion(M, Target, std::move(PS));
    if (llvm::failed(Result))
      signalPassFailure();
  }
};
}
