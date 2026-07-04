if (NOT CMAKE_TESTING_ENABLED)
 return()
endif()

add_custom_target(SchirUnitTests)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/unittests)

function(add_schir_unittest test_name)
  add_executable(${test_name} ${ARGN})
  target_link_libraries(${test_name}
    GTest::gtest
    GTest::gtest_main
    schir)
  add_dependencies(SchirUnitTests ${test_name})
  add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_schir_unittest(SchirValueTest
                   ValueTest.cpp
                   ContextTest.cpp
                   MangleTest.cpp)
