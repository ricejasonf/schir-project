// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_INTERFACES_H
#define SCHIR_INTERFACES_H

#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>
#include <mlir/Interfaces/FunctionInterfaces.h>

#include "schir/Interfaces/TypeInterfaces.h.inc"
#include "schir/Interfaces/OpInterfaces.h.inc"

namespace schir {
// Return true if T is a placeholder for a type that is not yet inferred.
inline bool isPlaceholder(mlir::Type T) {
  return llvm::isa<PlaceholderLike>(T);
}

inline bool isPlaceholder(mlir::Value V) {
  return isPlaceholder(V.getType());
}
} // namespace schir

#endif
