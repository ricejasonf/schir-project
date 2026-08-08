cmake_minimum_required(VERSION 3.31)

include_directories(include)

# Nbdl Dialect and heavy-scheme Plugin
add_subdirectory(include/nbdl_spec ${CMAKE_BINARY_DIR}/include/nbdl_spec)
add_subdirectory(lib)

set(NBDL_MODULE_PATH ${CMAKE_SOURCE_DIR}/nbdl/include)
add_subdirectory(test)
