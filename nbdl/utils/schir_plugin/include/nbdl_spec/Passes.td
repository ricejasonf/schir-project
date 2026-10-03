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

def FlattenPass : NbdlPass<"nbdl-flatten"> {
  let summary = "infer types and flatten continuations where possible";
  let description = [{
    Given optionally provided functions for C++ introspection, deduce
    types where possible and flatten operations where types are known
    to allow visitation without matching via the continuation passing
    style.
  }];
}

def LowerPass : NbdlPass<"nbdl-lower", "mlir::ModuleOp"> {
  let summary = "lower nbdl to mid level dialects";
  let description = [{
    Lower Nbdl operations to dialects that can be lowered
    to LLVM or SPIRV.

    Operations that require C++ cannot be lowered.
  }];
  let dependentDialects = ["nbdl_spec::NbdlDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::func::FuncDialect",
                           "mlir::memref::MemRefDialect",
                           "mlir::scf::SCFDialect"];
}

def LowerToLLVMPass : NbdlPass<"nbdl-to-llvm", "mlir::ModuleOp"> {
  let summary = "lower the dialects produced by nbdl-lower to LLVM";
  let description = [{
    Lower output dialects of nbdl-lower to the LLVM dialect.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect",
                           "mlir::affine::AffineDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def CAdapterPass : NbdlPass<"nbdl-c-adapter", "mlir::ModuleOp"> {
  let summary = "create C adapters for lowered functions called from C++";
  let description = [{
    For each llvm.func marked with the nbdl.c_adapter attribute (whose
    value is the function type before lowering), create an adapter with
    C linkage that receives a pointer to each result followed by a
    pointer to each argument and calls the lowered function.

    Pointers are passed the same way by every C ABI so C++ can call
    the adapter without depending on how the lowered prototype maps to
    the C ABI of the target. Each pointee has the layout of the C++ type
    mapped from the MLIR type.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
}

#endif // NBDL_DIALECT_PASSES_TD
