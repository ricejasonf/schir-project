// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_INTERFACES_H
#define SCHIR_COMMON_INTERFACES_H

#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>
#include <mlir/Interfaces/FunctionInterfaces.h>

namespace schir_common {
// Return the operation that defines V or whose region has V as an argument.
mlir::Operation* getIntroducingOp(mlir::Value V);

// Return the value that the ValueAccess of the operation that introduces V
// states V is read from or null.
mlir::Value getReadFrom(mlir::Value V);

// Follow getReadFrom from V back to a value that is not read from another.
mlir::Value getReadFromRoot(mlir::Value V);

// Return true if V is read from Of, directly or through others.
bool isReadFrom(mlir::Value V, mlir::Value Of);
} // namespace schir_common

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
