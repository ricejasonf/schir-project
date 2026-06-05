(import (schir base))

(define-library (schir llvm pass)
  (export
    inject-module)
  (import (schir base))
  (begin
    (load-plugin "SchirLLVMPass.so")
    (define inject-module
      (load-builtin "schir_llvm_pass_inject_module"))
    (define init
      (load-builtin "schir_llvm_pass_init"))
    (init)
    ))

