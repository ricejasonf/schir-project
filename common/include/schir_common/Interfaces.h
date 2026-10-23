// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_INTERFACES_H
#define SCHIR_COMMON_INTERFACES_H

#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>
#include <mlir/Interfaces/FunctionInterfaces.h>

#include "schir_common/TypeInterfaces.h.inc"
#include "schir_common/OpInterfaces.h.inc"

namespace schir_common {
// Return true if T is a placeholder for a type that is not yet inferred.
inline bool isPlaceholder(mlir::Type T) {
  return llvm::isa<PlaceholderLike>(T);
}

inline bool isPlaceholder(mlir::Value V) {
  return isPlaceholder(V.getType());
}
} // namespace schir_common

#endif
