// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_SPIRV_CONVERSION_H
#define SCHIR_COMMON_SPIRV_CONVERSION_H

#include <mlir/Dialect/SPIRV/IR/TargetAndABI.h>
#include <mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/PatternMatch.h>

namespace schir_common {
// Check that a vector type is valid with the capabilities of the target.
bool isSupportedSPIRVVector(mlir::spirv::TargetEnv const& TE,
                            mlir::VectorType VT);

// Convert vectors with no SPIRV vector type (e.g. vector<8xf32>) to arrays.
struct SPIRVTypeConverter : mlir::SPIRVTypeConverter {
  SPIRVTypeConverter(mlir::spirv::TargetEnvAttr TEA);
};

// Add patterns that construct arrays from elements before the upstream
// patterns that expect vectors.
void populateFromElementsToArrayPatterns(SPIRVTypeConverter& TC,
                                         mlir::RewritePatternSet& PS);
} // namespace schir_common

#endif
