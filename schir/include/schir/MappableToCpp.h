// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_MAPPABLE_TO_CPP_H
#define SCHIR_MAPPABLE_TO_CPP_H

#include <llvm/ADT/SmallVector.h>
#include <mlir/IR/Dialect.h>
#include <mlir/IR/DialectInterface.h>
#include <mlir/IR/Types.h>
#include <mlir/Support/TypeID.h>

namespace schir {
// Define a map for a set of mlir types in a dialect to a c++ type.
class MappableToCpp : public mlir::DialectInterface::Base<MappableToCpp> {
public:
  MappableToCpp(mlir::Dialect* D) : Base(D) {}

  virtual bool getCppTypename(mlir::Type T,
                              llvm::SmallVectorImpl<char>& Result) const = 0;

  // Append the fully qualified C++ typename for T to Result.
  // Return false if the type is not supported.
  static bool lookup(mlir::Type T, llvm::SmallVectorImpl<char>& Result) {
    auto* Interface = T.getDialect().getRegisteredInterface<MappableToCpp>();
    return Interface && Interface->getCppTypename(T, Result);
  }
};
} // namespace schir

// Defined in Mlir.cpp
MLIR_DECLARE_EXPLICIT_TYPE_ID(schir::MappableToCpp)

#endif
