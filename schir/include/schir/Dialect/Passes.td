// SchirDialectPasses.td - MLIR Dialect Pass Definitions for SchirScheme
//
// Copyright Jason Rice 2025
//
#ifndef SCHIR_DIALECT_PASSES_TD
#define SCHIR_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"

class SchirModulePass<string Name> : PassBase<Name, "schir::SchirModulePass">;

def StripGlobalBindingsPass : SchirModulePass<"schir-strip-global-bindings"> {
  let summary = "Strip bindings for global variables if they are never mutated";
  let description = [{
    If a binding object of a global variable is never mutated via the
    `set!` syntax, then we can elide the creation and unboxing of the binding.
  }];

  let dependentDialects = ["schir::SchirDialect"];
}

def StripLocalBindingsPass : SchirModulePass<"schir-strip-local-bindings"> {
  let summary = "Strip binding for local variables if they are never mutated";
  let description = [{
    If a binding is never mutated via the `set!` syntax after it is read
    for the first time, then we can elide the creation and unboxing of the
    binding.
  }];

  let dependentDialects = ["schir::SchirDialect"];
}

#endif  // SCHIR_DIALECT_PASSES_TD
