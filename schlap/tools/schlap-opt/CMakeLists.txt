cmake_minimum_required(VERSION 3.13.4)

add_executable(schlap-opt schlap_opt_main.cpp)

target_link_libraries(schlap-opt
  PRIVATE
  MLIR
  SchlapDialect
  SchlapTestDialect
  )

install(TARGETS schlap-opt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT schlap-opt)
