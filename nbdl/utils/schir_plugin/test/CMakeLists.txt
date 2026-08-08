configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  )

add_lit_testsuite(check-nbdl "Running Nbdl tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
  Nbdl
  schir-scheme
  schir
  SchirClang
  SchirLLVMPass
)
