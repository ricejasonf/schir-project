set(NBDL_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PREFIX}/lib/cmake/Nbdl" CACHE STRING
  "Path for CMake subdirectory for Nbdl when installing")
set(NBDL_CMAKE_BUILDDIR "${CMAKE_BINARY_DIR}/lib/cmake")
get_property(NBDL_EXPORTS GLOBAL PROPERTY NBDL_EXPORTS)
export(TARGETS ${NBDL_EXPORTS} FILE ${NBDL_CMAKE_BUILDDIR}/NbdlTargets.cmake)

# Generate build tree config file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NbdlConfig.cmake.in
  ${NBDL_CMAKE_BUILDDIR}/NbdlConfig.cmake)

# Generate installed tree config file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NbdlConfig.install.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NbdlConfig.cmake)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NbdlConfig.cmake
  DESTINATION ${NBDL_INSTALL_PACKAGE_DIR}
  COMPONENT cmake-nbdl-exports)
