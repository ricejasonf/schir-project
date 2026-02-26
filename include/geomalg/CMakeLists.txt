set(LLVM_TARGET_DEFINITIONS Geomalg.td)
mlir_tablegen(GeomalgOps.h.inc -gen-op-decls)
mlir_tablegen(GeomalgOps.cpp.inc -gen-op-defs)
mlir_tablegen(GeomalgTypes.h.inc -gen-typedef-decls)
mlir_tablegen(GeomalgTypes.cpp.inc -gen-typedef-defs)
mlir_tablegen(GeomalgDialect.h.inc -gen-dialect-decls -dialect geomalg)
mlir_tablegen(GeomalgDialect.cpp.inc -gen-dialect-defs -dialect geomalg)
add_public_tablegen_target(GeomalgOpsIncGen)
set_target_properties(GeomalgOpsIncGen PROPERTIES
  DESCRIPTION "Geomalg MLIR dialect")

# Attributes would need their own tablegen file since we
# use builtin attributes in the main tablegen file.
# set(LLVM_TARGET_DEFINITIONS GeomalgAttrs.td)
# mlir_tablegen(GeomalgAttrs.h.inc -gen-attrdef-decls)
# mlir_tablegen(GeomalgAttrs.cpp.inc -gen-attrdef-defs)

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(GeomalgPasses.h.inc -gen-pass-decls -name Geomalg)
add_public_tablegen_target(GeomalgPassesIncGen PROPERTIES
  DESCRIPTION "Geomalg MLIR transformation passes")
