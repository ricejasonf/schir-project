cmake_minimum_required(VERSION 3.13.4)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(AddSchir)

#set(MLIR_TABLEGEN_EXE $<TARGET_FILE:mlir-tblgen>)

if(POLICY CMP0075)
  cmake_policy(SET CMP0075 NEW)
endif()

set(SCHIR_GEN_INC ${CMAKE_BINARY_DIR}/tools/schir/include)
set(SCHIR_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CLANG_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../clang/lib)
include_directories(BEFORE
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
                    ${CMAKE_CURRENT_SOURCE_DIR}/../mlir/include
                    ${CMAKE_BINARY_DIR}/tools/mlir/include
                    ${SCHIR_GEN_INC}
                    ../clang/include)

# FIXME Copy tablegen headers for dialects
install(DIRECTORY include/schir
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT schir-headers
  FILES_MATCHING
  PATTERN "*.h")

add_custom_target(schir-headers)
      # DEPENDS schir-headers-tablegen)

add_llvm_install_targets(install-schir-headers
                         DEPENDS schir-headers
                         COMPONENT schir-headers)

# The scheme source files are found in installation
# include directories for the clang integration.
install(DIRECTORY include/schir
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT schir-scheme-libs
  FILES_MATCHING
  PATTERN "*.sld")

add_llvm_install_targets(install-schir-scheme-libs
                         DEPENDS schir-scheme-libs
                         COMPONENT schir-scheme-libs)

add_custom_target(schir-scheme-libs)

add_subdirectory(include/schir/Dialect)
add_subdirectory(lib)
add_subdirectory(tools/schir-scheme)
add_subdirectory(tools/schir-opt)
add_subdirectory(test)
add_subdirectory(unittests)
add_subdirectory(include/nbdl_gen)
add_subdirectory(plugins/HelloWorld)
add_subdirectory(plugins/SchirClang)

add_subdirectory(cmake/modules)
