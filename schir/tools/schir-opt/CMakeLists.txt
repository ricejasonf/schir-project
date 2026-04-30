cmake_minimum_required(VERSION 3.13.4)

add_llvm_executable(schir-opt schir_opt_main.cpp)
llvm_map_components_to_libnames(LLVM_LIBS core support)

target_link_libraries(schir-opt
  PRIVATE
  MLIRBytecodeWriter
  MLIRDebug
  MLIROptLib
  MLIRParser
  MLIRPass
  MLIRRemarkStreamer
  MLIRSupport
  schir
  )

add_llvm_install_targets(install-schir-opt
  DEPENDS schir-opt
  COMPONENT schir-opt)

install(TARGETS schir-opt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT schir-opt)
