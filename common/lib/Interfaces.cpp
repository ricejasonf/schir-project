// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#include <schir_common/Interfaces.h>

#include "schir_common/TypeInterfaces.cpp.inc"
#include "schir_common/OpInterfaces.cpp.inc"

mlir::Operation* schir_common::getIntroducingOp(mlir::Value V) {
  if (auto Arg = llvm::dyn_cast<mlir::BlockArgument>(V))
    return Arg.getOwner()->getParentOp();
  return V.getDefiningOp();
}

mlir::Value schir_common::getReadFrom(mlir::Value V) {
  auto Access = llvm::dyn_cast_if_present<ValueAccess>(getIntroducingOp(V));
  return Access ? Access.getReadFrom(V) : mlir::Value();
}

mlir::Value schir_common::getReadFromRoot(mlir::Value V) {
  while (mlir::Value From = getReadFrom(V))
    V = From;
  return V;
}

bool schir_common::isReadFrom(mlir::Value V, mlir::Value Of) {
  for (mlir::Value From = getReadFrom(V); From; From = getReadFrom(From))
    if (From == Of)
      return true;
  return false;
}
