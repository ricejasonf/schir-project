// Interfaces.td - Interfaces shared by dialects of schir-project
//
// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#ifndef SCHIR_COMMON_INTERFACES_TD
#define SCHIR_COMMON_INTERFACES_TD

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Type Interfaces
//===----------------------------------------------------------------------===//

def SchirCommon_PlaceholderLike : TypeInterface<"PlaceholderLike"> {
  let description = [{
    Mark a type as a placeholder for the type of a value that is not yet
    inferred. This allows operations from one dialect to accept values
    of a type that will be inferred by the passes of another dialect.
  }];
  let cppNamespace = "schir_common";
}

//===----------------------------------------------------------------------===//
// Operation Interfaces
//===----------------------------------------------------------------------===//

def SchirCommon_InferableReturn : OpInterface<"InferableReturn"> {
  let description = [{
    Mark a terminator as returning its operands from a function where
    the type of each operand may differ from the corresponding result
    type of the function until it is inferred (e.g. a placeholder) or
    converted (e.g. a narrowed type) by a pass. Unlike ReturnLike, this
    does not imply that the types match. This allows the passes of one
    dialect to infer or convert the values returned by the operations
    of another dialect.
  }];
  let cppNamespace = "schir_common";

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

def SchirCommon_ValueAccess : OpInterface<"ValueAccess"> {
  let description = [{
    State the value that each result or block argument of an operation is
    read from, the operands it mutates, and the operands it uses.
  }];
  let cppNamespace = "schir_common";

  let methods = [
    InterfaceMethod<[{
        Return the value that a value introduced by the operation is read
        from or null if it is not read from a value.
      }],
      "::mlir::Value", "getReadFrom", (ins "::mlir::Value":$introduced),
      "", [{ return {}; }]>,
    InterfaceMethod<[{
        Return true if the operation mutates the value of the operand.
      }],
      "bool", "isMutated", (ins "::mlir::OpOperand&":$operand),
      "", [{ return false; }]>,
    InterfaceMethod<[{
        Return true if the operation uses the value of the operand.
      }],
      "bool", "isUsed", (ins "::mlir::OpOperand&":$operand),
      "", [{ return true; }]>
  ];
}

#endif // SCHIR_COMMON_INTERFACES_TD
