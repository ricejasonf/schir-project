cmake_minimum_required(VERSION 3.31)

project(schir_project)

if (DEFINED LLVM_ROOT)
  set(MLIR_DIR ${LLVM_ROOT}/lib/cmake/mlir)
  set(Clang_DIR ${LLVM_ROOT}/lib/cmake/clang)
endif()

find_file(LLVM_EXTERNAL_LIT NAMES "lit")
# For local LLVM/MLIR specify MLIR_DIR LLVM_DIR on the command line.
find_package(MLIR REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)

set(SCHIR_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/schir/include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -std=c++26 -fno-rtti -fno-exceptions")
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/include)

add_subdirectory(schir)
