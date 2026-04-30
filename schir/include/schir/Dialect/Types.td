// Dialect/Types.td - MLIR Dialect Type Definitions for SchirScheme
//
// Copyright Jason Rice 2025
//
#ifndef SCHIR_DIALECT_TYPE_TD
#define SCHIR_DIALECT_TYPE_TD

include "mlir/IR/AttrTypeBase.td"

def Schir_Unknown : TypeDef<Schir_Dialect, "SchirUnknown", []> {
  let mnemonic = "unknown";
  let description = [{
    Define the opaquest of types.

    This is primarily used for datums when constructing syntax.
  }];
}

// Opaque value type that is not syntax or binding.
def Schir_Value : TypeDef<Schir_Dialect, "SchirValue", []> {
  let mnemonic = "value";
  let description = [{
    Define the opaque value type that is not a binding or syntax.
  }];
}

def Schir_Empty : TypeDef<Schir_Dialect, "SchirEmpty", []> {
  let mnemonic = "empty";
  let description = [{
    The empty list type analogous to schir::Empty.
    In scheme it is known by `null?`.
  }];
}

def Schir_Pair : TypeDef<Schir_Dialect, "SchirPair", []> {
  let mnemonic = "pair";
  let description = [{
    The pair type for linked lists
  }];
}

def Schir_Procedure : TypeDef<Schir_Dialect, "SchirProcedure", []> {
  let mnemonic = "procedure";
  let description = [{
    The closure type aka lambda
  }];
}

def Schir_Vector : TypeDef<Schir_Dialect, "SchirVector", []> {
  let mnemonic = "vector";
  let description = [{
    The vector type -- a run-time lengthed tuple-like type
  }];
}

def Schir_Undefined : TypeDef<Schir_Dialect, "SchirUndefined", []> {
  let mnemonic = "undefined";
  let description = [{
    The unspecified type the report is always talking about
  }];
}

// Not value types

def Schir_Context : TypeDef<Schir_Dialect, "SchirContext", []> {
  let mnemonic = "context";
  let description = [{
    The context object represents the current continuation
    along with the state of the entire scheme run-time.
  }];
}

def Schir_ValueRefs : TypeDef<Schir_Dialect, "SchirValueRefs", []> {
  let mnemonic = "value_refs";
  let description = [{
    Represent a mutable contiguous array view to values that are typically
    the arguments to the current function call.
  }];
}

def Schir_Rest : TypeDef<Schir_Dialect, "SchirRest", []> { // For rest arguments.
  let mnemonic = "rest";
  let description = [{
    Represent the dynamically lengthed linked list of arguments that
    form the rest parameter.
  }];
}

def Schir_Syntax : TypeDef<Schir_Dialect, "SchirSyntax", []> {
  let mnemonic = "syntax";
  let description = [{
    Represent a syntax object that can appear as a callee
    in a syntax.
  }];
}

def Schir_Binding : TypeDef<Schir_Dialect, "SchirBinding", []> {
  let mnemonic = "binding";
  let description = [{
    A binding is an indirection on a value that must be "unboxed"
    for use as a normal scheme value.
  }];
}

#endif // SCHIR_DIALECT_TYPE_TD
