cmake_minimum_required(VERSION 3.13.4)

add_llvm_pass_plugin(SchirMlirAllPasses SchirMlirAllPasses.cpp)

find_package(MLIR REQUIRED CONFIG)
target_include_directories(SchirMlirAllPasses
  SYSTEM PRIVATE ${MLIR_INCLUDE_DIRS})
