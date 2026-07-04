cmake_minimum_required(VERSION 3.31)

project(schir_project)

if (DEFINED SCHIR_LLVM_ROOT)
  file(REAL_PATH "${SCHIR_LLVM_ROOT}" SCHIR_LLVM_ROOT EXPAND_TILDE)
  set(MLIR_DIR ${SCHIR_LLVM_ROOT}/lib/cmake/mlir)
  set(Clang_DIR ${SCHIR_LLVM_ROOT}/lib/cmake/clang)
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

include_directories(${CMAKE_SOURCE_DIR}/schir/include)

set(SCHIR_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/schir/include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -std=c++26 -fno-rtti -fno-exceptions")
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/include)

find_package(GTest)
if (GTest_FOUND)
  enable_testing()
endif()

add_subdirectory(schir)
add_subdirectory(geomalg)
add_subdirectory(nbdl/tool/schir_plugins)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})
add_dependencies(check check-schir)
add_dependencies(check check-geomalg)
