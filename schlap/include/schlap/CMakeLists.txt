set(LLVM_TARGET_DEFINITIONS Schlap.td)
mlir_tablegen(SchlapOps.h.inc -gen-op-decls)
mlir_tablegen(SchlapOps.cpp.inc -gen-op-defs)
mlir_tablegen(SchlapTypes.h.inc -gen-typedef-decls)
mlir_tablegen(SchlapTypes.cpp.inc -gen-typedef-defs)
mlir_tablegen(SchlapDialect.h.inc -gen-dialect-decls -dialect schlap)
mlir_tablegen(SchlapDialect.cpp.inc -gen-dialect-defs -dialect schlap)
add_public_tablegen_target(SchlapOpsIncGen)
set_target_properties(SchlapOpsIncGen PROPERTIES
  DESCRIPTION "Schlap MLIR dialect")

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(SchlapPasses.h.inc -gen-pass-decls -name Schlap)
add_public_tablegen_target(SchlapPassesIncGen PROPERTIES
  DESCRIPTION "Schlap MLIR passes")
