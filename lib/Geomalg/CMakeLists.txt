cmake_minimum_required(VERSION 3.13.4)

# TODO Put common cpp files in separate target.

add_library(GeomalgDialect Type.cpp Dialect.cpp Passes.cpp)
target_compile_options(GeomalgDialect PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(GeomalgDialect GeomalgOpsIncGen
                                GeomalgPassesIncGen
                                GeomalgTransformsExpand)

add_library(Geomalg MODULE Type.cpp Geomalg.cpp Dialect.cpp Passes.cpp)
target_link_libraries(Geomalg
  MLIRPass
  MLIRRegisterAllDialects
  MLIRRegisterAllExtensions
  MLIRRegisterAllPasses)
target_compile_options(Geomalg PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(Geomalg GeomalgOpsIncGen
                         GeomalgPassesIncGen
                         GeomalgTransformsExpand)

install(TARGETS Geomalg
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS GeomalgDialect
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
