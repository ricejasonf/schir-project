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
  LLVMCore
  LLVMSupport
  MLIRAnalysis
  MLIRBytecodeReader
  MLIRBytecodeWriter
  MLIRFuncDialect
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSideEffectInterfaces
  MLIRSupport
  MLIRTransforms

  MLIRRegisterAllDialects
  MLIRRegisterAllPasses
  MLIRRegisterAllExtensions
  ${MLIR_DIALECT_LIBS}
  ${MLIR_CONVERSION_LIBS}
  ${MLIR_EXTENSION_LIBS})
