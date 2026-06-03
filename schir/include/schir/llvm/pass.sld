(import (schir base))

(define-library (schir llvm pass)
  (export
    inject-module)
  (import (schir base))
  (begin
    (load-plugin "SchirLLVMPass.so")
    (define inject_module
      (load_builtin "schir_llvm_pass_inject_module")
    ))

