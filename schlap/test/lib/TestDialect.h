#ifndef SCHLAP_TEST_DIALECT_H
#define SCHLAP_TEST_DIALECT_H

#include <mlir/IR/Dialect.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/OpImplementation.h>

#include "TestDialect.h.inc"

#define GET_OP_CLASSES
#include "TestOps.h.inc"

namespace schlap_test {
// Register the test dialect and attach the ValueAccess interface to its
// operations.
void registerTestDialect(mlir::DialectRegistry& Registry);
}  // namespace schlap_test

#endif  // SCHLAP_TEST_DIALECT_H
