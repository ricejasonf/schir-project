#include <geomalg/Dialect.h>
#include <geomalg/Metric.h>
#include <mlir/Conversion/ArithToSPIRV/ArithToSPIRV.h>
#include <mlir/Conversion/FuncToSPIRV/FuncToSPIRV.h>
#include <mlir/Conversion/VectorToSPIRV/VectorToSPIRV.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Linalg/IR/Linalg.h>
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
#include "geomalg/GeomalgPasses.h.inc"
}

namespace geomalg {
// Implemented in Passes.cpp
mlir::LogicalResult
applyUpdateReturnPatterns(mlir::Operation* Op);
}

namespace arith = mlir::arith;
namespace func = mlir::func;
namespace linalg = mlir::linalg;
namespace spirv = mlir::spirv;
namespace tensor = mlir::tensor;
namespace vector = mlir::vector;
using namespace geomalg;

namespace {
mlir::ValueRange expandVector(mlir::RewriterBase& R, mlir::Location Loc,
                              mlir::Value InputVector) {
  auto VT = dyn_cast<mlir::VectorType>(InputVector.getType());
  assert(VT && VT.getRank() == 1 &&
      "expecting a 1-d vector");
  assert(R.getContext()->getLoadedDialect<vector::VectorDialect>());
  auto NewOp = vector::ToElementsOp::create(R, Loc, InputVector);
  return NewOp.getResults();
}

// All types will be converted a single scalar type
// and tensors over said scalar type.
struct TypeConverter : mlir::TypeConverter {
  TypeConverter(mlir::Type ScalarT)
    : mlir::TypeConverter()
  {
    addConversion([ScalarT](BladeType BT) { return ScalarT; });
    addConversion([ScalarT](ZeroType ZT) { return ScalarT; });
    addConversion([ScalarT](MultivectorLike MV) {
      int64_t Size = MV.getBlades().size();
      return mlir::VectorType::get(Size, ScalarT);
      //return mlir::RankedTensorType::get(Size, ScalarT);
    });
  }
};

struct ConversionTarget : mlir::ConversionTarget {
  ConversionTarget(mlir::MLIRContext& Ctx)
    : mlir::ConversionTarget(Ctx)
  {
    // Legalize stuff.
    addLegalDialect<arith::ArithDialect>();
    addLegalDialect<linalg::LinalgDialect>();
    addLegalDialect<func::FuncDialect>();
    addLegalDialect<vector::VectorDialect>();
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

struct LowerReturn : mlir::OpConversionPattern<ReturnOp>,
                     ::PatternBase {
public:
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        ReturnOp Op, ReturnOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Location Loc = Op->getLoc();
    mlir::Type ScalarT = getScalarT();

    auto FuncOp = dyn_cast<mlir::func::FuncOp>(Op->getParentOp());
    if (!FuncOp)
      return llvm::failure();


    mlir::Value NewResult = Adaptor.getArg();
    R.replaceOpWithNewOp<mlir::func::ReturnOp>(Op, NewResult);

    // Update the function signature so the new func::ReturnOp will be valid.
    // This means replacing the entire FuncOp.
    mlir::FunctionType FT = FuncOp.getFunctionType();
    mlir::TypeConverter const* TC = getTypeConverter();
    mlir::TypeConverter::SignatureConversion SC(FT.getNumInputs());
    if (llvm::failed(TC->convertSignatureArgs(FT.getInputs(), SC)))
        return R.notifyMatchFailure(Op,
            "failed to remap function argument types");
    mlir::FunctionType NewFT = mlir::FunctionType::get(
        R.getContext(), SC.getConvertedTypes(), NewResult.getType());
    FuncOp.setFunctionType(NewFT);
    R.applySignatureConversion(&FuncOp.getBody().front(), SC);

    return llvm::success();
  }
};

void populateLowerPasses(mlir::RewritePatternSet& PS,
                         ::TypeConverter const* TC) {
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<CMulToMul,
         SumToVector,
         ExpandToVector,
         LowerNegate,
         LowerBlade,
         DotToArith,
         LowerReturn
         >(*TC, Ctx);
}

llvm::LogicalResult
applyLowerPatterns(mlir::MLIRContext* Ctx, mlir::Operation* Op) {
  // Fix return types of geomalg::ReturnOps.
  if (llvm::failed(applyUpdateReturnPatterns(Op)))
    return llvm::failure();

  // Do that actual Conversion.
  ::ConversionTarget Target(*Ctx);

  mlir::RewritePatternSet PS(Ctx);
  mlir::Type ScalarT = mlir::Float32Type::get(Ctx);
  // Note that TypeConverter has its pointer captured.
  ::TypeConverter TC(ScalarT);
  populateLowerPasses(PS, &TC);

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

    llvm::LogicalResult Result = applyLowerPatterns(Ctx, M);
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

    mlir::LogicalResult
      Result = mlir::applyPartialConversion(M, *Target, std::move(PS));
    if (llvm::failed(Result))
      signalPassFailure();
  }
};
}  // namespace

