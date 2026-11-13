set(LLVM_TARGET_DEFINITIONS TestDialect.td)
mlir_tablegen(TestOps.h.inc -gen-op-decls)
mlir_tablegen(TestOps.cpp.inc -gen-op-defs)
mlir_tablegen(TestDialect.h.inc -gen-dialect-decls -dialect schlap_test)
mlir_tablegen(TestDialect.cpp.inc -gen-dialect-defs -dialect schlap_test)
add_public_tablegen_target(SchlapTestOpsIncGen)

# Define a dialect that only schlap-opt registers.
add_library(SchlapTestDialect STATIC TestDialect.cpp)
target_compile_options(SchlapTestDialect PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(SchlapTestDialect SchlapTestOpsIncGen SchirCommonIncGen)
target_include_directories(SchlapTestDialect
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(SchlapTestDialect PRIVATE SchirCommon)
