// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_LOWER_H
#define SCHIR_COMMON_LOWER_H

#include <mlir/IR/BuiltinOps.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/Support/LogicalResult.h>

namespace mlir {
class LLVMTypeConverter;
class RewritePatternSet;
} // namespace mlir

namespace schir_common {
struct SPIRVTypeConverter;

// Add the type conversions and patterns of other dialects to a lowering.
using PopulateLLVMFn =
  llvm::function_ref<void(mlir::LLVMTypeConverter&, mlir::RewritePatternSet&)>;
using PopulateSPIRVFn =
  llvm::function_ref<void(SPIRVTypeConverter&, mlir::RewritePatternSet&)>;

// Lower the arith, func, vector, math, scf, cf, memref and affine
// dialects (and those of Populate) to the LLVM dialect.
llvm::LogicalResult lowerToLLVM(mlir::ModuleOp M,
                                PopulateLLVMFn Populate = nullptr);

// Lower the arith, func, vector, math, scf, cf and memref dialects
// (and those of Populate) to the SPIRV dialect.
llvm::LogicalResult lowerToSPIRV(mlir::ModuleOp M,
                                 PopulateSPIRVFn Populate = nullptr);
} // namespace schir_common

#endif
