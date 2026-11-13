// TestDialect.td - A dialect to test rules about IR in schlap-opt
//
// Copyright Jason Rice 2026
//
#ifndef SCHLAP_TEST_DIALECT_TD
#define SCHLAP_TEST_DIALECT_TD

include "mlir/IR/OpBase.td"

def SchlapTest_Dialect : Dialect {
  let name = "schlap_test";
  let cppNamespace = "::schlap_test";
}

class Test_Op<string mnemonic, list<Trait> traits = []> :
    Op<SchlapTest_Dialect, mnemonic, traits>;

def Test_DefOp : Test_Op<"def"> {
  let description = [{
    Create a value that is not read from another.
  }];
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` type($result)";
}

def Test_ReadOp : Test_Op<"read"> {
  let description = [{
    Read a value from the operand.
  }];
  let arguments = (ins AnyType:$from);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$from attr-dict `:` type($from) `->` type($result)";
}

def Test_MutateOp : Test_Op<"mutate"> {
  let description = [{
    Mutate the operands.
  }];
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "($values^ `:` type($values))? attr-dict";
}

def Test_UseOp : Test_Op<"use"> {
  let description = [{
    Use the operands.
  }];
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "($values^ `:` type($values))? attr-dict";
}

def Test_ForgetOp : Test_Op<"forget"> {
  let description = [{
    Take the operands without using them.
  }];
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "($values^ `:` type($values))? attr-dict";
}

def Test_BranchOp : Test_Op<"branch", [NoTerminator]> {
  let description = [{
    Continue with one of the regions, whose arguments are read from the
    operand.
  }];
  let arguments = (ins AnyType:$from);
  let regions = (region VariadicRegion<SizedRegion<1>>:$branches);
  let assemblyFormat = "$from `:` type($from) $branches attr-dict";
}

def Test_ScopeOp : Test_Op<"scope", [NoTerminator]> {
  let description = [{
    Continue with the region.
  }];
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$body attr-dict";
}

#endif  // SCHLAP_TEST_DIALECT_TD
