cmake_minimum_required(VERSION 3.31)

project(geomalg_project)

if (DEFINED SCHIR_ROOT AND NOT DEFINED Schir_DIR)
  set(Schir_DIR ${SCHIR_ROOT}/lib/cmake/schir)
endif()
find_package(Schir REQUIRED CONFIG)

find_file(LLVM_EXTERNAL_LIT NAMES "lit")
find_package(MLIR REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${SCHIR_INCLUDE_DIRS})

set(GEOMALG_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -std=c++26 -fno-rtti -fno-exceptions")
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/include)

add_subdirectory(include/geomalg)
add_subdirectory(lib/Geomalg)
add_subdirectory(tools/geomalg-opt)
add_subdirectory(test)
