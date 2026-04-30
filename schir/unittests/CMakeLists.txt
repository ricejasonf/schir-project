add_custom_target(SchirUnitTests)
function(add_schir_unittest test_name)
  add_unittest(SchirUnitTests ${test_name} ${ARGN})
endfunction()

add_schir_unittest(SchirValueTest
                   ValueTest.cpp
                   ContextTest.cpp
                   MangleTest.cpp)

target_link_libraries(
  SchirValueTest
  PRIVATE
  schir
)
