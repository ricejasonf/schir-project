cmake_minimum_required(VERSION 3.13.4)

# Code shared by the plugins of schir-project.
add_library(SchirCommon STATIC
  Interfaces.cpp
  SPIRVConversion.cpp)
set_property(TARGET SchirCommon PROPERTY POSITION_INDEPENDENT_CODE ON)
# LLVM and MLIR are not linked so plugins may resolve them from the host.
add_dependencies(SchirCommon SchirCommonIncGen)
