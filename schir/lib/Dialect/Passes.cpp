//===------------------------ Passes.cpp ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <schir/Dialect.h>
#include <schir/Dialect/Passes.h>
#include <mlir/IR/Operation.h>

namespace schir {
#define GEN_PASS_DEF_STRIPGLOBALBINDINGSPASS
// TODO
//#define GEN_PASS_DEF_STRIPLOCALBINDINGSPASS
#include <schir/Dialect/SchirPasses.h.inc>
}

using namespace schir;

namespace {
struct StripGlobalBindingsPass
  : public schir::impl::StripGlobalBindingsPassBase<StripGlobalBindingsPass> {
  void runOnOperation() override;
  bool isSetFound(mlir::Value OpResult);
};

// Strip any schir.unbox whose input is
// no longer possibly a binding.
// (which is invalid.)
void stripUnboxes(mlir::ModuleOp Op) {
  Op->walk([](schir::UnboxOp UnboxOp) {
      mlir::Value Input = UnboxOp.getBinding();
      if (!isa<SchirBindingType, SchirUnknownType>(Input.getType())) {
        UnboxOp.getResult().replaceAllUsesWith(Input);
        UnboxOp.erase();
      }
      return mlir::WalkResult::advance();
    });
}

// Strip schir.match_type that maps to the same type.
void stripMatchTypes(mlir::ModuleOp Op) {
  Op->walk([](schir::MatchTypeOp MatchTypeOp) {
      mlir::Value Input = MatchTypeOp.getArg();
      if (Input.getType() == MatchTypeOp.getResult().getType()) {
        MatchTypeOp.getResult().replaceAllUsesWith(Input);
        MatchTypeOp.erase();
      }
      return mlir::WalkResult::advance();
    });
}
}  // namespace


void StripGlobalBindingsPass::runOnOperation() {
  mlir::ModuleOp ModuleOp = getOperation();
  for (mlir::Operation& TopLevelOp : ModuleOp.getBody()->getOperations()) {
    if (auto GlobalOp = dyn_cast<schir::GlobalOp>(&TopLevelOp)) {
      mlir::Type Type = GlobalOp.getType();
      if (GlobalOp.isExternal() ||
          !isa<schir::SchirBindingType>(Type))
        continue;

      bool IsSetFound = false;
      ModuleOp->walk([&](schir::LoadGlobalOp LG) -> mlir::WalkResult {
          if (LG.getName() == GlobalOp.getSymName() &&
              isSetFound(LG.getResult()))
            IsSetFound = true;
          return mlir::WalkResult::advance();
        });

      // Find any SetOp that uses the result of said LoadGlobalOp.
      if (IsSetFound)
        continue;

      // Strip the binding.
      GlobalOp->walk([&](schir::InitGlobalOp IG) -> mlir::WalkResult {
          if (auto B = IG.getVar().getDefiningOp<schir::BindingOp>()) {
            Type = B.getInput().getType();
            IG.setOperand(B.getInput());
            B.erase();
          } else {
            IG->emitOpError("schir.init_global input type did not match "
                            "annotated type of schir.global_op");
          }
          // There should be only one InitGlobalOp.
          return mlir::WalkResult::interrupt();
        });

      // Update the GlobalOp annotated type.
      GlobalOp.setType(Type);

      // Update the type of the schir.load_globals.
      ModuleOp->walk([&](schir::LoadGlobalOp LG) -> mlir::WalkResult {
          if (LG.getName() == GlobalOp.getSymName()) {
            LG.getResult().setType(Type);
            // Update any captures. (Yes, capturing globals is useful.)
            updateCaptureTypes(LG.getResult());
          }
          return mlir::WalkResult::advance();
        });

      stripUnboxes(ModuleOp);
      stripMatchTypes(ModuleOp);
    }
  }
}

// Recursively chase operation results to see if we find a "use" by a SetOp.
bool StripGlobalBindingsPass::isSetFound(mlir::Value OpResult) {
  assert(OpResult);
  // Because globals can be loaded anywhere, any SetOp requires
  // the variable to exist as a binding.
  for (mlir::OpOperand& Use : OpResult.getUses()) {
    assert(Use.getOwner());
    if (isa<schir::SetOp>(Use.getOwner()))
      return true;
    if (mlir::Value Capture = getCapture(Use)) {
      if (isSetFound(Capture))
        return true;
    }
  }
  return false;
}

// If Operand is being captured, get the result of the
// LoadRefOp that loads the captured Operand.
// Otherwise, return mlir::Value().
mlir::Value SchirModulePass::getCapture(mlir::OpOperand& Operand) {
  assert(Operand.get());
  mlir::Operation* Op = Operand.getOwner();
  unsigned Index = Operand.getOperandNumber();

  schir::FuncOp FuncOp = lookupCapturingFunction(Op);
  if (!FuncOp)  // Not a capture
    return mlir::Value();

  // Find the LoadRefOp with the corresponding Index.
  mlir::Value Result;
  FuncOp->walk([&](schir::LoadRefOp LoadRefOp) {
      if (LoadRefOp.getIndex() == Index) {
        Result = LoadRefOp.getResult();
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });

  if (!Result)
    Op->emitOpError("capture operand #") << Index <<
                    "does not have corresponding schir.load_ref";
  return Result;
}

// Recursively, chase captures of a value updating the type
// the corresponding schir.load_refs.
void SchirModulePass::updateCaptureTypes(mlir::Value Value) {
  for (mlir::OpOperand& Use : Value.getUses()) {
    assert(Use.getOwner());
    if (mlir::Value Capture = getCapture(Use)) {
      Capture.setType(Value.getType());
      updateCaptureTypes(Capture);
    }
  }
}

// If Op is a capturing operation, then get the function name
// and perform lookup in the current ModuleOp.
schir::FuncOp SchirModulePass::lookupCapturingFunction(mlir::Operation* Op) {
  llvm::StringRef FuncName;
  if (auto PC = dyn_cast<schir::PushContOp>(Op))
    FuncName = PC.getName();
  else if (auto L = dyn_cast<schir::LambdaOp>(Op))
    FuncName = L.getName();

  if (FuncName.empty())  // Not a capture
    return schir::FuncOp();

  mlir::ModuleOp M = getOperation();
  mlir::Operation* FoundOp = M.lookupSymbol(FuncName);
  auto FuncOp = llvm::dyn_cast_if_present<schir::FuncOp>(FoundOp);
  if (!FuncOp || FuncOp.getBody().empty()) {
    Op->emitOpError("operation refers to capturing function "
                    "with no definition in the current module");
    return schir::FuncOp();
  }
  return FuncOp;
}
