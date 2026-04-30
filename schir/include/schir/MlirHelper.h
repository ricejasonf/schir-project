//===---- MlirHelper.h - Mlir binding helper functions ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares helper functions for binding mlir objects in schir scheme.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_MLIR_HELPER_H
#define LLVM_SCHIR_MLIR_HELPER_H

#include <schir/Context.h>
#include <schir/Mlir.h>
#include <schir/Value.h>
#include <mlir/IR/Value.h>
#include <llvm/ADT/StringRef.h>

namespace mlir {
  class OpBuilder;
  class Operation;
}

namespace schir::mlir_bind_var {
extern schir::ContextLocal current_context;
extern schir::ContextLocal current_builder;
}

namespace schir::mlir_helper {
mlir::MLIRContext* getCurrentContext(schir::Context& C);
mlir::OpBuilder* getBuilder(schir::Context& C, schir::Value V);
mlir::OpBuilder* getCurrentBuilder(schir::Context& C);
mlir::Operation* getSingleOpArg(schir::Context& C, schir::ValueRefs Args);
void with_builder_impl(Context& C, mlir::OpBuilder const& Builder,
                       schir::Value Thunk);

}  // end namespace schir::mlir_helper


#endif
