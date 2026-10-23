// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#include <schir_common/Lower.h>
#include <schir_common/SPIRVConversion.h>
#include <mlir/Conversion/AffineToStandard/AffineToStandard.h>
#include <mlir/Conversion/ArithToLLVM/ArithToLLVM.h>
#include <mlir/Conversion/ArithToSPIRV/ArithToSPIRV.h>
#include <mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h>
#include <mlir/Conversion/ControlFlowToSPIRV/ControlFlowToSPIRV.h>
#include <mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h>
#include <mlir/Conversion/FuncToSPIRV/FuncToSPIRV.h>
#include <mlir/Conversion/LLVMCommon/ConversionTarget.h>
#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Conversion/MathToLLVM/MathToLLVM.h>
#include <mlir/Conversion/MathToSPIRV/MathToSPIRV.h>
#include <mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h>
#include <mlir/Conversion/MemRefToSPIRV/MemRefToSPIRV.h>
#include <mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h>
#include <mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h>
#include <mlir/Conversion/SCFToSPIRV/SCFToSPIRV.h>
#include <mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h>
#include <mlir/Conversion/VectorToSPIRV/VectorToSPIRV.h>
#include <mlir/Dialect/MemRef/Transforms/Transforms.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/SPIRV/IR/TargetAndABI.h>
#include <mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <llvm/ADT/SmallVector.h>
#include <memory>

namespace memref = mlir::memref;
namespace scf = mlir::scf;
namespace spirv = mlir::spirv;

namespace schir_common {
llvm::LogicalResult lowerToLLVM(mlir::ModuleOp M) {
  mlir::MLIRContext* Ctx = M.getContext();

  // Expand memref view operations (e.g. subview) into operations
  // on their strided metadata which MemRefToLLVM can lower.
  {
    mlir::RewritePatternSet PS(Ctx);
    memref::populateExpandStridedMetadataPatterns(PS);
    if (llvm::failed(mlir::applyPatternsGreedily(M, std::move(PS))))
      return llvm::failure();
  }

  mlir::LLVMTypeConverter TC(Ctx);
  mlir::LLVMConversionTarget Target(*Ctx);

  mlir::RewritePatternSet PS(Ctx);
  mlir::populateAffineToStdConversionPatterns(PS);
  mlir::populateSCFToControlFlowConversionPatterns(PS);
  mlir::arith::populateArithToLLVMConversionPatterns(TC, PS);
  mlir::cf::populateControlFlowToLLVMConversionPatterns(TC, PS);
  mlir::populateMathToLLVMConversionPatterns(TC, PS);
  mlir::populateFinalizeMemRefToLLVMConversionPatterns(TC, PS);
  mlir::populateFuncToLLVMConversionPatterns(TC, PS);
  mlir::populateVectorToLLVMConversionPatterns(TC, PS);

  if (llvm::failed(mlir::applyPartialConversion(M, Target, std::move(PS))))
    return llvm::failure();

  // Remove casts between types that are made equivalent.
  llvm::SmallVector<mlir::UnrealizedConversionCastOp> Casts;
  M.walk([&](mlir::UnrealizedConversionCastOp Op) { Casts.push_back(Op); });
  mlir::reconcileUnrealizedCasts(Casts);
  return llvm::success();
}

llvm::LogicalResult lowerToSPIRV(mlir::ModuleOp M) {
  mlir::MLIRContext* Ctx = M.getContext();

  // Inline the regions of scf.execute_region which has no SPIRV lowering.
  {
    mlir::RewritePatternSet PS(Ctx);
    scf::ExecuteRegionOp::getCanonicalizationPatterns(PS, Ctx);
    if (llvm::failed(mlir::applyPatternsGreedily(M, std::move(PS))))
      return llvm::failure();
  }

  spirv::TargetEnvAttr TEA = spirv::lookupTargetEnvOrDefault(M);
  SPIRVTypeConverter TC(TEA);
  std::unique_ptr<mlir::SPIRVConversionTarget>
    Target = mlir::SPIRVConversionTarget::get(TEA);

  mlir::ScfToSPIRVContext ScfContext;
  mlir::RewritePatternSet PS(Ctx);
  populateFromElementsToArrayPatterns(TC, PS);
  mlir::populateSCFToSPIRVPatterns(TC, ScfContext, PS);
  mlir::arith::populateArithToSPIRVPatterns(TC, PS);
  mlir::cf::populateControlFlowToSPIRVPatterns(TC, PS);
  mlir::populateFuncToSPIRVPatterns(TC, PS);
  mlir::populateBuiltinFuncToSPIRVPatterns(TC, PS);
  mlir::populateMathToSPIRVPatterns(TC, PS);
  mlir::populateMemRefToSPIRVPatterns(TC, PS);
  mlir::populateVectorToSPIRVPatterns(TC, PS);

  return mlir::applyPartialConversion(M, *Target, std::move(PS));
}
} // namespace schir_common
