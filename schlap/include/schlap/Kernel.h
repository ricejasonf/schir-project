#ifndef SCHLAP_KERNEL_H
#define SCHLAP_KERNEL_H

#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <string>

namespace schlap {
// Return the source name of the variable bound at a location or "".
using VarNameFn = llvm::function_ref<std::string(mlir::Location)>;

// Map each declaration the kernel accepted to the axioms it depends on.
using AcceptedDecls = llvm::DenseMap<mlir::Operation*, mlir::ArrayAttr>;

// Check every theorem, definition and axiom in Module whatever attributes
// it carries.
llvm::LogicalResult checkProofs(mlir::ModuleOp Module, VarNameFn VarName);

// Check Decl against the declarations before it that are in Accepted and
// add it to Accepted if it checks.
llvm::LogicalResult checkDeclaration(mlir::Operation* Decl,
                                     AcceptedDecls& Accepted,
                                     VarNameFn VarName);
}  // namespace schlap

#endif  // SCHLAP_KERNEL_H
