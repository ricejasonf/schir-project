set(LLVM_TARGET_DEFINITIONS Geomalg.td)
mlir_tablegen(GeomalgOps.h.inc -gen-op-decls)
mlir_tablegen(GeomalgOps.cpp.inc -gen-op-defs)
mlir_tablegen(GeomalgTypes.h.inc -gen-typedef-decls)
mlir_tablegen(GeomalgTypes.cpp.inc -gen-typedef-defs)
mlir_tablegen(GeomalgTypeInterfaces.h.inc -gen-type-interface-decls)
mlir_tablegen(GeomalgTypeInterfaces.cpp.inc -gen-type-interface-defs)
mlir_tablegen(GeomalgDialect.h.inc -gen-dialect-decls -dialect geomalg)
mlir_tablegen(GeomalgDialect.cpp.inc -gen-dialect-defs -dialect geomalg)
mlir_tablegen(GeomalgEnums.h.inc -gen-enum-decls)
mlir_tablegen(GeomalgEnums.cpp.inc -gen-enum-defs)
# Only generate our own attributes since we use builtin attributes.
mlir_tablegen(GeomalgAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=geomalg)
mlir_tablegen(GeomalgAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=geomalg)
add_public_tablegen_target(GeomalgOpsIncGen)
set_target_properties(GeomalgOpsIncGen PROPERTIES
  DESCRIPTION "Geomalg MLIR dialect")

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(GeomalgPasses.h.inc -gen-pass-decls -name Geomalg)
add_public_tablegen_target(GeomalgPassesIncGen PROPERTIES
  DESCRIPTION "Geomalg MLIR transformation passes")
