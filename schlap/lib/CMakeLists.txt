cmake_minimum_required(VERSION 3.13.4)

set(SCHLAP_SOURCE_GEN_DEPS
  SchlapOpsIncGen
  SchlapPassesIncGen
  SchirCommonIncGen)

set(SCHLAP_SOURCE_FILES
  Schlap/Dialect.cpp;
  Schlap/Infer.cpp;
  Schlap/Normalize.cpp;
  Schlap/Check.cpp;
  Schlap/Kernel.cpp;
  Schlap/ApplyRule.cpp;
  Schlap/Fold.cpp;
  Schlap/SpecOps.cpp)

add_library(SchlapDialect ${SCHLAP_SOURCE_FILES})
target_compile_options(SchlapDialect PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(SchlapDialect ${SCHLAP_SOURCE_GEN_DEPS})
target_link_libraries(SchlapDialect
  PRIVATE
    SchirCommon
    LLVM
    MLIR)

add_library(Schlap MODULE Schlap.cpp ${SCHLAP_SOURCE_FILES})
target_link_libraries(Schlap PRIVATE SchirCommon LLVM MLIR)
target_compile_options(Schlap PRIVATE -fno-rtti -fno-exceptions)
add_dependencies(Schlap ${SCHLAP_SOURCE_GEN_DEPS})

install(TARGETS Schlap
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS SchlapDialect
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
