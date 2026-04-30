configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  )

add_lit_testsuite(check-schir "Running Schir tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS schir-scheme schir-opt SchirClang SchirHelloWorld FileCheck
)
