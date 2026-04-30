cmake_minimum_required(VERSION 3.13.4)

add_executable(schir-opt schir_opt_main.cpp)

# Generated includes
target_include_directories(schir-opt
  PRIVATE
  ${CMAKE_BINARY_DIR}/schir/include)

target_link_libraries(schir-opt
  PRIVATE
  LLVMCore
  LLVMSupport
  MLIRBytecodeWriter
  MLIRDebug
  MLIROptLib
  MLIRParser
  MLIRPass
  MLIRRemarkStreamer
  MLIRSupport
  schir
  )

install(TARGETS schir-opt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT schir-opt)
