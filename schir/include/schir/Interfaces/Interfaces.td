// Interfaces.td - Interfaces shared by dialects using Schir
//
// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_INTERFACES_TD
#define SCHIR_INTERFACES_TD

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Type Interfaces
//===----------------------------------------------------------------------===//

def Schir_PlaceholderLike : TypeInterface<"PlaceholderLike"> {
  let description = [{
    Mark a type as a placeholder for the type of a value that is not yet
    inferred. This allows operations from one dialect to accept values
    of a type that will be inferred by the passes of another dialect.
  }];
  let cppNamespace = "schir";
}

//===----------------------------------------------------------------------===//
// Operation Interfaces
//===----------------------------------------------------------------------===//

def Schir_InferableReturn : OpInterface<"InferableReturn"> {
  let description = [{
    Mark a terminator as returning its operands from a function where
    the type of each operand may differ from the corresponding result
    type of the function until it is inferred (e.g. a placeholder) or
    converted (e.g. a narrowed type) by a pass. Unlike ReturnLike, this
    does not imply that the types match. This allows the passes of one
    dialect to infer or convert the values returned by the operations
    of another dialect.
  }];
  let cppNamespace = "schir";

  let methods = [
    InterfaceMethod<[{
        Return the function that this operation returns from
        or null if it terminates a region of some other operation.
      }],
      "::mlir::FunctionOpInterface", "getReturnedFunction", (ins), "",
      [{
        return ::llvm::dyn_cast<::mlir::FunctionOpInterface>(
                                    $_op->getParentOp());
      }]>,
    InterfaceMethod<[{
        Return the operands that are the values returned.
      }],
      "::llvm::MutableArrayRef<::mlir::OpOperand>", "getReturnedOperands",
      (ins), "", [{ return $_op->getOpOperands(); }]>
  ];

  let verify = [{
    if (!$_op->template hasTrait<::mlir::OpTrait::IsTerminator>())
      return $_op->emitOpError("InferableReturn must be a terminator");
    return ::mlir::success();
  }];
}

#endif // SCHIR_INTERFACES_TD
