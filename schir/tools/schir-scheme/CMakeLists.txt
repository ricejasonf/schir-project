cmake_minimum_required(VERSION 3.13.4)

add_llvm_executable(schir-scheme schir_main.cpp)
llvm_map_components_to_libnames(LLVM_LIBS core support)

target_link_libraries(schir-scheme PRIVATE schir ${LLVM_LIBS})
set_target_properties(schir-scheme PROPERTIES ENABLE_EXPORTS 1)
            
add_llvm_install_targets(install-schir-scheme
  DEPENDS schir-scheme
  COMPONENT schir-scheme)

install(TARGETS schir-scheme
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT schir-scheme)
