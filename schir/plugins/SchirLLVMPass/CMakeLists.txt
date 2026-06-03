cmake_minimum_required(VERSION 3.13.4)

add_llvm_pass_plugin(SchirLLVMPass SchirLLVMPass.cpp)

find_package(LLVM REQUIRED CONFIG)
target_include_directories(SchirLLVMPass
  SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
