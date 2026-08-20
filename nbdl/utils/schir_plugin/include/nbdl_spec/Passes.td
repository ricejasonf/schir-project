// NbdlDialectPasses.td - MLIR Dialect Pass Definitions for Nbdl
//
// Copyright Jason Rice 2026
//
#ifndef NBDL_DIALECT_PASSES_TD
#define NBDL_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"
include "mlir/Rewrite/PassUtil.td"

class NbdlPass<string name, string operation = "">
: Pass<name, operation> {
  let dependentDialects = ["nbdl_spec::NbdlDialect",
                           "mlir::func::FuncDialect"];
}

def FlattenPass : NbdlPass<"nbdl-flatten", "mlir::func::FuncOp"> {
  let summary = "infer types and flatten continuations where possible";
  let description = [{
    Given optionally provided functions for C++ introspection, deduce
    types where possible and flatten operations where types are known
    to allow visitation without matching via the continuation passing
    style.
  }];
}

#endif // NBDL_DIALECT_PASSES_TD
