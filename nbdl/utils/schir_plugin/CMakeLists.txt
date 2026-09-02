cmake_minimum_required(VERSION 3.31)

include_directories(include)

# Nbdl Dialect and heavy-scheme Plugin
add_subdirectory(include/nbdl_spec ${CMAKE_BINARY_DIR}/include/nbdl_spec)
add_subdirectory(lib)

add_subdirectory(test)
