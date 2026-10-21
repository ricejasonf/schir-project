; RUN: not schir-scheme -I %schir_module_path -I %geomalg_module_path %s 2>&1 | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base)
        (geomalg versor)
        (geomalg motor))

; Report an odd versor at the nlerp syntax.
; CHECK: motor-errors.scm:[[@LINE+4]]:3: error: operation failed verification
; CHECK: motor-errors.scm:[[@LINE+3]]:3: note: 'geomalg.interp' op expecting an even versor operand
; CHECK-NEXT: (nlerp (versor) (versor a) 0.5))
(define-func bad_nlerp ((a : !e1))
  (nlerp (versor) (versor a) 0.5))
