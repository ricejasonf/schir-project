#ifndef SCHLAP_DIALECT_H
#define SCHLAP_DIALECT_H

#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Dialect.h>
#include <mlir/IR/OpDefinition.h>
#include <mlir/IR/OpImplementation.h>
#include <mlir/Interfaces/SideEffectInterfaces.h>

#include "schlap/SchlapDialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "schlap/SchlapTypes.h.inc"

#define GET_OP_CLASSES
#include "schlap/SchlapOps.h.inc"

#endif  // SCHLAP_DIALECT_H
