cmake_minimum_required(VERSION 3.13.4)

add_executable(geomalg-opt geomalg_opt_main.cpp)

target_link_libraries(geomalg-opt
  PRIVATE
  MLIRBytecodeWriter
  MLIRDebug
  MLIROptLib
  MLIRParser
  MLIRPass
  MLIRRemarkStreamer
  MLIRSupport

  MLIRRegisterAllDialects
  MLIRRegisterAllPasses
  GeomalgDialect
  )

install(TARGETS geomalg-opt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT geomalg-opt)
