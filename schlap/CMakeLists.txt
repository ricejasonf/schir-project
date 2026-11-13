cmake_minimum_required(VERSION 3.31)

include_directories(${CMAKE_SOURCE_DIR}/schlap/include)

add_subdirectory(include/schlap ${CMAKE_BINARY_DIR}/include/schlap)
add_subdirectory(lib)
add_subdirectory(test/lib)
add_subdirectory(tools/schlap-opt)
add_subdirectory(test)
