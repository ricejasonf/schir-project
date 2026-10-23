// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_LOWER_H
#define SCHIR_COMMON_LOWER_H

#include <mlir/IR/BuiltinOps.h>
#include <llvm/Support/LogicalResult.h>

namespace schir_common {
// Lower the arith, func, vector, math, scf, cf, memref and affine
// dialects to the LLVM dialect.
llvm::LogicalResult lowerToLLVM(mlir::ModuleOp M);

// Lower the arith, func, vector, math, scf, cf and memref dialects
// to the SPIRV dialect.
llvm::LogicalResult lowerToSPIRV(mlir::ModuleOp M);
} // namespace schir_common

#endif
