; RUN: not schir-scheme -I %schir_module_path -I %geomalg_module_path %s 2>&1 | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base))

(with-metric 'cga)

; Report an inverse of an odd multivector at the inverse syntax.
; CHECK: inverse-errors.scm:[[@LINE+3]]:13: error: failed to apply metric to function: bad_inverse
; CHECK: inverse-errors.scm:[[@LINE+3]]:3: note: 'geomalg.inverse' op expecting a blade, 1-vector, versor, or even multivector operand
; CHECK-NEXT: (inverse (sum a B)))
(define-func bad_inverse ((a : !e1) (B : (!blade !e1 !e2 !e3)))
  (inverse (sum a B)))
