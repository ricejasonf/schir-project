//===- Dialect.h - Classes for representing declarations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Declares the mlir Dialect including generated classes
//
//===----------------------------------------------------------------------===//

#ifndef SCHIR_DIALECT_H
#define SCHIR_DIALECT_H

#include "schir/Value.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/PointerLikeTypeTraits.h"

// In Value we assume Operation* has the same alignment as ValueBase*.
// (which should be 8 bytes)
// Check that here
static_assert(llvm::PointerLikeTypeTraits<schir::Operation*>::NumLowBitsAvailable ==
              schir::ValueSumType::OperationTraits::NumLowBitsAvailable,
              "mlir::Operation* must have 8 byte alignment to fit in schir::Value");


namespace schir {
using mlir::func::FuncOp;
using StringAttr = mlir::StringAttr;
}

namespace schir::detail {
struct SchirValueAttrStorage;
}

// #pragma clang diagnostic push
// #pragma clang diagnostic ignored "-Wunused-parameter"

#include "schir/Dialect/SchirDialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "schir/Dialect/SchirTypes.h.inc"

#define GET_ATTRDEF_CLASSES
#include "schir/Dialect/SchirAttrs.h.inc"

#define GET_OP_CLASSES
#include "schir/Dialect/SchirOps.h.inc"
// #pragma clang diagnostic pop

#endif
