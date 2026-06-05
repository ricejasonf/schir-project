(import (schir base))

(define-library (schir mlir all-passes)
  (export
    register-all-passes
    run-passes)
  (import (schir base))
  (begin
    (load-plugin "SchirMlirAllPasses.so")
    (define register-all-passes
      (load-builtin "schir_mlir_all_passes_register_all_passes"))
    (define run-passes
      (load-builtin "schir_mlir_all_passes_run_passes"))
    ))
