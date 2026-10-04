// PlaceholderLike.td - Type interfaces shared by dialects using Schir
//
// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_PLACEHOLDER_LIKE_TD
#define SCHIR_PLACEHOLDER_LIKE_TD

include "mlir/IR/OpBase.td"

def Schir_PlaceholderLike : TypeInterface<"PlaceholderLike"> {
  let description = [{
    Mark a type as a placeholder for the type of a value that is not yet
    inferred. This allows operations from one dialect to accept values
    of a type that will be inferred by the passes of another dialect.
  }];
  let cppNamespace = "schir";
}

#endif // SCHIR_PLACEHOLDER_LIKE_TD
