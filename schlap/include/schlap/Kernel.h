#ifndef SCHLAP_KERNEL_H
#define SCHLAP_KERNEL_H

#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/OwningOpRef.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <string>

namespace schlap {
// Return the source name of the variable bound at a location or "".
using VarNameFn = llvm::function_ref<std::string(mlir::Location)>;

// Check every theorem, definition and axiom in Module whatever attributes
// it carries.
llvm::LogicalResult checkProofs(mlir::ModuleOp Module, VarNameFn VarName);

class KernelState;

// Check Decl against the declarations State accepted, add a copy of Decl to
// State if it checks, and return the axioms Decl depends on.
llvm::FailureOr<mlir::ArrayAttr> checkDeclaration(mlir::Operation* Decl,
                                                  KernelState& State,
                                                  VarNameFn VarName);

// Hold copies of the declarations the kernel accepted where only the kernel
// can reach them.
class KernelState {
  mlir::OwningOpRef<mlir::ModuleOp> Module;

  friend llvm::FailureOr<mlir::ArrayAttr>
  checkDeclaration(mlir::Operation* Decl, KernelState& State,
                   VarNameFn VarName);

public:
  explicit KernelState(mlir::MLIRContext* Context);
};
}  // namespace schlap

#endif  // SCHLAP_KERNEL_H
