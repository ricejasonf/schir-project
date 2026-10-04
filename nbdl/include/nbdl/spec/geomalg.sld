(import (schir builtins))

(define-library (nbdl spec geomalg)
  (export)
  (import (schir base)
          (nbdl spec)
          ;; Load the Geomalg dialect and passes.
          (prefix (geomalg base) geomalg-))
  (begin
    ;; Infer the types of Geomalg operations in functions
    ;; (e.g. defined with define-fn) with the inference passes.
    ;; Just force CGA metric for now since it is the only use case.
    (register-inference-pass "geomalg-expand{metric=cga}")

    ;; Lower Geomalg operations in functions lowered to LLVM.
    (register-lowering-pass "geomalg-lower")

    ));
