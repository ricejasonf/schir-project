cmake_minimum_required(VERSION 3.31)

set(GEOMALG_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/geomalg/include)

add_subdirectory(include/geomalg ${CMAKE_BINARY_DIR}/include/geomalg)
add_subdirectory(lib/Geomalg)
add_subdirectory(tools/geomalg-opt)
add_subdirectory(test)
