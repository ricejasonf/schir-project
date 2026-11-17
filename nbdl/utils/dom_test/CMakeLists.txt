add_custom_command(
  OUTPUT ${dom_test_dir}/index.js
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR} ${dom_test_dir}
)

add_custom_command(
  OUTPUT ${dom_test_dir}/node_modules
  COMMAND npm install --silent
  DEPENDS ${dom_test_dir}/index.js
  WORKING_DIRECTORY ${dom_test_dir}
  COMMENT "Resolve npm deps for dom_test."
)

add_custom_target(utils.dom_test
  DEPENDS
    ${dom_test_dir}/index.js
    ${dom_test_dir}/node_modules
)

