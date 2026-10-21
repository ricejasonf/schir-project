; RUN: not schir-scheme -I %schir_module_path -I %geomalg_module_path %s 2>&1 | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base)
        (geomalg conformal))

(with-metric 'cga)

; Report a conversion that drops blades at the convert syntax.
; CHECK: convert-errors.scm:[[@LINE+3]]:13: error: failed to apply metric to function: bad_convert
; CHECK: convert-errors.scm:[[@LINE+3]]:3: note: 'geomalg.convert' op cannot convert '!geomalg.multivector<<1>, <2>, <4>, <8>, <16>>' to '!geomalg.multivector<<1>, <2>, <4>>'
; CHECK-NEXT: (convert (vprod (up x) (plane n d)) !vec3))
(define-func bad_convert ((x : !vec3) (n : !vec3) (d : !scalar))
  (convert (vprod (up x) (plane n d)) !vec3))
