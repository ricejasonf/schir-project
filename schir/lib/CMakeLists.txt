cmake_minimum_required(VERSION 3.13.4)

add_library(schir
  Builtins.cpp
  CharInfo.cpp
  Context.cpp
  Dialect.cpp
  Dialect/Passes.cpp
  Heap.cpp
  SchirScheme.cpp
  Lexer.cpp
  Mangle.cpp
  Mlir.cpp
  MlirHelper.cpp
  OpEval.cpp
  OpGen.cpp
  Parser.cpp
  Quasiquote.cpp
  SourceFileStorage.cpp
  SourceManager.cpp)

# To support plugins
set_property(TARGET schir PROPERTY POSITION_INDEPENDENT_CODE ON)

# Include TableGen generated files.
target_include_directories(schir
  PRIVATE
  ${CMAKE_BINARY_DIR}/schir/include)

add_dependencies(schir
  SchirDialectIncGen
  SchirPassesIncGen)

target_link_libraries(schir
  PRIVATE
    LLVM
    MLIR)

# Interfaces shared by the dialects of plugins.
add_library(SchirInterfaces STATIC
  Interfaces/Interfaces.cpp)
set_property(TARGET SchirInterfaces PROPERTY POSITION_INDEPENDENT_CODE ON)
# LLVM and MLIR are not linked so plugins may resolve them from the host.
add_dependencies(SchirInterfaces SchirInterfacesIncGen)
