cmake_minimum_required(VERSION 3.13.4)

set(GEOMALG_SOURCE_GEN_DEPS
  GeomalgOpsIncGen
  GeomalgPassesIncGen
  SchirCommonIncGen)

set(GEOMALG_SOURCE_FILES
  Geomalg/Type.cpp;
  Geomalg/Dialect.cpp;
  Geomalg/Passes.cpp;
  Geomalg/Lower.cpp)

add_library(GeomalgDialect ${GEOMALG_SOURCE_FILES})
target_compile_options(GeomalgDialect PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(GeomalgDialect ${GEOMALG_SOURCE_GEN_DEPS})
target_link_libraries(GeomalgDialect
  PRIVATE
    SchirCommon
    LLVM
    MLIR)

add_library(Geomalg MODULE Geomalg.cpp ${GEOMALG_SOURCE_FILES})
target_link_libraries(Geomalg PRIVATE SchirCommon LLVM MLIR)
target_compile_options(Geomalg PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(Geomalg ${GEOMALG_SOURCE_GEN_DEPS})

install(TARGETS Geomalg
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS GeomalgDialect
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
