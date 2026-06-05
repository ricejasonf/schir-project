cmake_minimum_required(VERSION 3.13.4)

set(GEOMALG_SOURCE_GEN_DEPS
  GeomalgOpsIncGen
  GeomalgPassesIncGen)

set(GEOMALG_SOURCE_FILES
  Type.cpp;
  Dialect.cpp;
  Passes.cpp;
  Lower.cpp)

add_library(GeomalgDialect ${GEOMALG_SOURCE_FILES})
target_compile_options(GeomalgDialect PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(GeomalgDialect ${GEOMALG_SOURCE_GEN_DEPS})
target_link_libraries(GeomalgDialect
  PRIVATE
    LLVMCore
    LLVMSupport
    MLIR)

add_library(Geomalg MODULE Geomalg.cpp ${GEOMALG_SOURCE_FILES})
target_link_libraries(Geomalg
  PRIVATE
    LLVMCore
    LLVMSupport
    MLIR)
target_compile_options(Geomalg PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(Geomalg ${GEOMALG_SOURCE_GEN_DEPS})

install(TARGETS Geomalg
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS GeomalgDialect
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
