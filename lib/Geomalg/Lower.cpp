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
class TypeConverter : mlir::TypeConverter {
  mlir::Type ScalarT;
public:
  TypeConverter(mlir::Type ScalarT)
    : mlir::TypeConverter(),
      ScalarT(ScalarT)
  {
    addConversion([=](BladeType BT) { return ScalarT; });
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

#if 0  // TODO If we need TypeConverter anyways we could
       //      use it to specify the scalar type with this
       //      explicit object parameter thingy I have been
       //      wanting to use.
struct PatternBase {
  template <typename Self>
  mlir::Type getScalarT(this Self& self) {
    mlir::MLIRContext* Ctx = &self.getContext();
    mlir::TypeConverter* TC = self.getTypeConverter();
    mlir::Type BT = BladeType::get(Ctx, 0);
    return TC->convertType(BT);
  }
};
#endif

// Conversion Patterns

class CMulToMul : public mlir::OpConversionPattern<CMulOp> {
  mlir::Type ScalarT;

public:
  template <typename ...Args>
  CMulToMul(mlir::Type ScalarT, Args ...args)
    : Base(args...),
      ScalarT(ScalarT)
  { }

  llvm::LogicalResult matchAndRewrite(
        CMulOp Op, CMulOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::ValueRange Args = Adaptor.getArgs();
    if (Args.empty()) {
      // Return a constant 1.0f.
      R.replaceOpWithNewOp<arith::ConstantOp>(Op, R.getOneAttr(ScalarT));
    } else {
      mlir::Value Result = Args.front();
      for (mlir::Value Arg : Args.drop_front())
        Result = arith::MulFOp::create(R, Op.getLoc(), ScalarT, Result, Arg);
      R.replaceOp(Op, Result);
    }
    return llvm::success();
  }
};

class LowerPass : public geomalg::impl::LowerPassBase<LowerPass> {
  using Base = geomalg::impl::LowerPassBase<LowerPass>;
  mlir::FrozenRewritePatternSet Patterns;

public:
  using Base::Base;
  llvm::LogicalResult initialize(mlir::MLIRContext* Ctx) override {
    mlir::RewritePatternSet PS(Ctx);
    mlir::Type ScalarT = mlir::Float32Type::get(Ctx);
    PS.add<CMulToMul>(ScalarT, Ctx);
    Patterns = mlir::FrozenRewritePatternSet(std::move(PS));
    return llvm::success();
  }

  void runOnOperation() override {
    mlir::ModuleOp M = getOperation();
    ConversionTarget Target(getContext());
    mlir::LogicalResult
      Result = mlir::applyPartialConversion(M, Target, Patterns);
    if (llvm::failed(Result))
      signalPassFailure();
  }
};
}
