set(LLVM_TARGET_DEFINITIONS PlaceholderLike.td)
mlir_tablegen(PlaceholderLike.h.inc -gen-type-interface-decls)
add_public_tablegen_target(SchirInterfacesIncGen)
