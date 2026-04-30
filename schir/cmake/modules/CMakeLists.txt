set(SCHIR_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/schir" CACHE STRING
  "Path for CMake subdirectory for Schir (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/schir')")
set(schir_cmake_builddir "${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/schir")
get_property(SCHIR_EXPORTS GLOBAL PROPERTY SCHIR_EXPORTS)
export(TARGETS ${SCHIR_EXPORTS} FILE ${schir_cmake_builddir}/SchirTargets.cmake)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install_distribution_exports(Schir)
endif()
