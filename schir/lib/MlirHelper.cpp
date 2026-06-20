//===------------ MlirHelper.cpp - MlirHelper stuff -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Defines helper functions for mlir scheme bindings.
//
//===----------------------------------------------------------------------===//

#include <schir/Context.h>
#include <schir/Mlir.h>
#include <schir/MlirHelper.h>
#include <schir/Value.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/Operation.h>

namespace schir::mlir_helper {
mlir::MLIRContext* getCurrentContext(schir::Context& C) {
  return any_cast<mlir::MLIRContext*>(
      mlir_bind_var::current_context.get(C));
}

mlir::OpBuilder* getBuilder(schir::Context& C, schir::Value V) {
  if (auto* OpBuilder = any_cast<mlir::OpBuilder>(&V))
    return OpBuilder;

  C.RaiseError("current-builder is invalid");
  return nullptr;
}

mlir::OpBuilder* getCurrentBuilder(schir::Context& C) {
  schir::Value V = mlir_bind_var::current_builder.get(C);
  return getBuilder(C, V);
}

mlir::Operation* getSingleOpArg(schir::Context& C, schir::ValueRefs Args) {
  mlir::Operation* Op = nullptr;
  if (Args.size() == 1)
    Op = schir::dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    C.RaiseError("expecting mlir operation");
  return Op;
}

void WithBuilderImpl(Context& C, mlir::OpBuilder const& Builder,
                     schir::Value Thunk) {
  schir::Value PrevBuilder = C.CreateBinding(schir::Empty());
  schir::Value NewBuilder = C.CreateAny(Builder);

  schir::Value Before = C.CreateLambda(
    [](schir::Context& C, schir::ValueRefs Args) {
      // Save the previous state and instate the new... state.
      // (ie Builder)
      auto* PrevBuilder = cast<schir::Binding>(C.getCapture(0));
      schir::Value NewBuilder = C.getCapture(1);

      // Set the Binding.
      PrevBuilder->setValue(mlir_bind_var::current_builder.get(C));

      // Set the "current-builder" value.
      mlir_bind_var::current_builder.set(C, NewBuilder);
      C.Cont();
    }, CaptureList{PrevBuilder, NewBuilder});

  schir::Value After = C.CreateLambda(
    [](schir::Context& C, schir::ValueRefs Args) {
      // Restore previous state
      auto* PrevBuilder = cast<schir::Binding>(C.getCapture(0));
      mlir_bind_var::current_builder.set(C, PrevBuilder->getValue());
      C.Cont();
    }, CaptureList{PrevBuilder});

  C.DynamicWind(Before, Thunk, After);
}

llvm::LogicalResult WithDiagnosticsHandler(
                               schir::Context& C,
                               llvm::function_ref<llvm::LogicalResult()> Thunk,
                               llvm::StringRef ErrorMsg,
                               schir::Value Irr) {
  mlir::MLIRContext* MCtx = getCurrentContext(C);
  // Attach mlir diagnostics as "notes" to the scheme error
  // to be raised if PassManager::run fails.
  llvm::SmallVector<schir::Value, 1> Irrs;
  if (!isa<schir::Undefined>(Irr))
    Irrs.push_back(Irr);
  mlir::ScopedDiagnosticHandler DH(MCtx,
      [&](mlir::Diagnostic& D) -> llvm::LogicalResult {
        std::string ErrMsg = D.str();
        mlir::Location ErrLoc = D.getLocation();
        auto Loc = schir::SourceLocation(mlir::OpaqueLoc
          ::getUnderlyingLocationOrNull<
              schir::SourceLocationEncoding*>(ErrLoc));
        schir::Value Error = C.CreateError(Loc, llvm::StringRef(ErrMsg),
                                           schir::Empty());
        Irrs.push_back(Error);
        return llvm::failure();
      });

  if (mlir::failed(Thunk())) {
    C.RaiseError(ErrorMsg, Irrs);
    return llvm::failure();
  }
  return llvm::success();
}

}  // end namespace schir::mlir_helper
