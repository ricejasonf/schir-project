cmake_minimum_required(VERSION 3.13.4)

add_llvm_library(SchirClang MODULE SchirClang.cpp PLUGIN_TOOL clang)

# Use  -DClang_DIR=llvm_build/lib/cmake/clang.
# (case sensitive)
find_package(Clang REQUIRED CONFIG)
target_include_directories(SchirClang
  PRIVATE ${CLANG_INCLUDE_DIRS})
target_link_libraries(SchirClang PRIVATE schir)
