#ifndef SCHLAP_APPLY_RULE_H
#define SCHLAP_APPLY_RULE_H

#include <schlap/Dialect.h>
#include <mlir/IR/Builders.h>
#include <mlir/Interfaces/FunctionInterfaces.h>
#include <llvm/Support/LogicalResult.h>

namespace schlap {
// Return the body of a function with a body or of another symbol
// operation with one region, or null for any other operation.
mlir::Region* getRuleBody(mlir::Operation* Op);

// Create the specification of the paths through a function that a rule
// states, at the insertion point of the builder.
llvm::FailureOr<SpecOp> applyRule(RuleOp Rule, mlir::Operation* Fn,
                                  mlir::OpBuilder& Builder);
}  // namespace schlap

#endif  // SCHLAP_APPLY_RULE_H
