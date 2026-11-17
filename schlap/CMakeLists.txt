cmake_minimum_required(VERSION 3.31)

include_directories(${CMAKE_SOURCE_DIR}/schlap/include)

# Install the Scheme libraries and user-facing C++ headers.
install(DIRECTORY include/schlap
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING
  PATTERN "*.sld"
  PATTERN "*.hpp")

add_subdirectory(include/schlap ${CMAKE_BINARY_DIR}/include/schlap)
add_subdirectory(lib)
add_subdirectory(test/lib)
add_subdirectory(tools/schlap-opt)
add_subdirectory(test)
