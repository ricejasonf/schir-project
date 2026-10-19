; RUN: schir-scheme -I %schir_module_path -I %geomalg_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) + - * /)
        (schir mlir)
        (geomalg base)
        (geomalg infix)
        (geomalg versor))

; The !versor type flattens the factors of versors.
; CHECK-LABEL: func.func @make_versor(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.versor<!geomalg.blade<2>, !geomalg.multivector<<1>, <2>>>)
; CHECK-NEXT: [[BC:%[0-9]+]] = "geomalg.versor"([[B]], [[C]])
; CHECK-SAME: -> !geomalg.versor<!geomalg.blade<1>, !geomalg.blade<2>, !geomalg.multivector<<1>, <2>>>
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.versor"([[A]], [[BC]])
; CHECK-SAME: -> !geomalg.versor<!geomalg.multivector<<1>, <2>, <4>>, !geomalg.blade<1>, !geomalg.blade<2>, !geomalg.multivector<<1>, <2>>>
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func make_versor ((a : !vec3)
                          (b : !e1)
                          (c : (!versor (!versor !e2) !vec2)))
  (versor a (versor b c)))
(write (module-lookup geomalg-current-module "make_versor"))
(newline)

; CHECK-LABEL: func.func @apply_versor(
; CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>,
; CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor<!geomalg.blade<1>, !geomalg.multivector<<1>, <2>>>)
; CHECK-NEXT: [[VP:%[0-9]+]] = "geomalg.vprod"([[X]], [[V]])
; CHECK-NEXT: geomalg.return [[VP]]
(define-func apply_versor ((x : !vec3) (v : (!versor !e1 !vec2)))
  (vprod x v))
(write (module-lookup geomalg-current-module "apply_versor"))
(newline)

; CHECK-LABEL: func.func @versor_gprod(
; CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>,
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<1>)
; CHECK-NEXT: [[V:%[0-9]+]] = "geomalg.versor"([[A]], [[B]])
; CHECK-NEXT: [[GP:%[0-9]+]] = "geomalg.gprod"([[V]], [[X]])
; CHECK-NEXT: geomalg.return [[GP]]
(define-func versor_gprod ((x : !vec3) (a : !vec3) (b : !e1))
  {(versor a b) * x})
(write (module-lookup geomalg-current-module "versor_gprod"))
(newline)

; Versor products expand with a metric.
(with-metric 'cga)

; CHECK-LABEL: func.func @apply_versor_cga(
; CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
; CHECK-NOT: "geomalg.versor"
; CHECK-NOT: "geomalg.vprod"
; CHECK: geomalg.return
(define-func apply_versor_cga ((x : !vec3) (a : !e1) (b : !e2))
  (vprod x (versor a b)))
(write (module-lookup geomalg-current-module "apply_versor_cga"))
(newline)
