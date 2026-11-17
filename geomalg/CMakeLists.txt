cmake_minimum_required(VERSION 3.31)

include_directories(${CMAKE_SOURCE_DIR}/geomalg/include)

# Install the Scheme libraries and user-facing C++ headers.
install(DIRECTORY include/geomalg
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING
  PATTERN "*.sld"
  PATTERN "*.hpp")

add_subdirectory(include/geomalg ${CMAKE_BINARY_DIR}/include/geomalg)
add_subdirectory(lib)
add_subdirectory(tools/geomalg-opt)
add_subdirectory(test)
