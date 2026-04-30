cmake_minimum_required(VERSION 3.13.4)

add_executable(schir-scheme schir_main.cpp)

target_link_libraries(schir-scheme
  PRIVATE
  schir
  LLVMCore
  LLVMSupport)
set_target_properties(schir-scheme PROPERTIES ENABLE_EXPORTS 1)
            
install(TARGETS schir-scheme
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT schir-scheme)
