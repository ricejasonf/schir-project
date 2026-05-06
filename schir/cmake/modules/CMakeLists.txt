set(SCHIR_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PREFIX}/lib/cmake/schir" CACHE STRING
  "Path for CMake subdirectory for Schir when installing")
set(SCHIR_CMAKE_BUILDDIR "${CMAKE_BINARY_DIR}/lib/cmake/schir")
get_property(SCHIR_EXPORTS GLOBAL PROPERTY SCHIR_EXPORTS)
export(TARGETS ${SCHIR_EXPORTS} FILE ${SCHIR_CMAKE_BUILDDIR}/SchirTargets.cmake)

# Generate build tree config file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/SchirConfig.cmake.in
  ${SCHIR_CMAKE_BUILDDIR}/SchirConfig.cmake)

# Generate installed tree config file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/SchirConfig.install.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/SchirConfig.cmake)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/SchirConfig.cmake
  DESTINATION ${SCHIR_INSTALL_PACKAGE_DIR}
  COMPONENT cmake-schir-exports)
