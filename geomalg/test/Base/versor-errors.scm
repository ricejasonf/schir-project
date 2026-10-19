; RUN: not schir-scheme -I %schir_module_path -I %geomalg_module_path %s 2>&1 | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base)
        (geomalg versor))

; Report invalid factors at the versor syntax.
; CHECK: versor-errors.scm:[[@LINE+4]]:3: error: operation failed verification
; CHECK: versor-errors.scm:[[@LINE+3]]:3: note: 'geomalg.versor' op operand #1 must be variadic of 1-vector or versor, but got '!geomalg.blade<3>'
; CHECK-NEXT: (versor a b))
(define-func bad_factor ((a : !e1) (b : (!blade !e1 !e2)))
  (versor a b))
