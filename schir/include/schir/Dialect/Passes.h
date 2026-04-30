//===------------------------ Passes.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCHIR_DIALECT_PASSES_H
#define SCHIR_DIALECT_PASSES_H

#include <schir/Dialect.h>
#include <mlir/Pass/Pass.h>

namespace schir {
// Base class for passes need to chase continuation
// and lambda functions within a module.
class SchirModulePass : public mlir::OperationPass<mlir::ModuleOp> {
  using Base = mlir::OperationPass<mlir::ModuleOp>;

public:
  using Base::OperationPass;
  mlir::Value getCapture(mlir::OpOperand& Operand);
  schir::FuncOp lookupCapturingFunction(mlir::Operation* Op);
  void updateCaptureTypes(mlir::Value);
};
}

// Generated stuff
namespace schir {
#define GEN_PASS_DECL
#include "schir/Dialect/SchirPasses.h.inc"

#define GEN_PASS_REGISTRATION
#include "schir/Dialect/SchirPasses.h.inc"
}  // namespace schir

#endif  // SCHIR_DIALECT_PASSES_H
