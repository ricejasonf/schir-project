; RUN: schir-scheme -I %schir_module_path -I %geomalg_module_path %s | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base))

; CHECK-LABEL: func.func @my_func1(
; CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[ARG1:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[ARG2:%arg[0-9]+]]: !geomalg.blade<3>,
; CHECK-SAME: [[ARG3:%arg[0-9]+]]: !geomalg.blade<2147483651>)
; CHECK-SAME: -> !geomalg.unknown {
; CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[ARG2]], [[ARG3]])
; CHECK-NEXT: [[SUM1:%[0-9]+]] = "geomalg.sum"([[ARG0]], [[ARG1]], [[SUM0]])
; CHECK-NEXT: geomalg.return [[SUM1]]
(define-func my_func1 ((arg0 : !e1)
                       (arg1 : !e2)
                       (arg2 : (!blade !e1 !e2))
                       (arg3 : (!blade !e2 !e1)))
  (sum arg0
       arg1
       (sum arg2 arg3)
       ))
(write (module-lookup geomalg-current-module "my_func1"))
(newline)

; CHECK-LABEL: func.func @my_func2(
; CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.blade<7>,
; CHECK-SAME:[[ARG1:%arg[0-9]+]]: !geomalg.blade<2147483655>)
; CHECK-SAME:-> !geomalg.unknown {
; CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[ARG0]], [[ARG1]])
; CHECK-NEXT: geomalg.return [[SUM0]]
(define-func my_func2 ((arg0 : (!blade !e1 !e2 !e3))
                       (arg1 : (!blade !e3 !e2 !e1)))
  (sum arg0
       arg1
       ))
(write (module-lookup geomalg-current-module "my_func2"))
(newline)

; CHECK-LABEL: func.func @my_func3(
; CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.blade<23>,
; CHECK-SAME: [[ARG1:%arg[0-9]+]]: !geomalg.blade<23>)
; CHECK-SAME: -> !geomalg.unknown {
; CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[ARG0]], [[ARG1]])
; CHECK-NEXT: geomalg.return [[SUM0]]
(define-func my_func3 ((arg0 : (!blade !e1 !e2 !e3 !ni))
                       (arg1 : (!blade !ni !e3 !e2 !e1)))
  (sum arg0
       arg1
       ))
(write (module-lookup geomalg-current-module "my_func3"))
(newline)

; CHECK-LABEL: func.func @my_func4
; CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <3>>)
; CHECK-SAME: -> !geomalg.unknown
(define-func my_func4 ((arg0 : (!multivector !e1 !e2 (!blade !e1 !e2))))
  arg0)
(write (module-lookup geomalg-current-module "my_func4"))
(newline)

; CHECK: func.func @my_func5() -> !geomalg.unknown
; CHECK-NEXT: [[E1:%[0-9]+]] = "geomalg.blade"()
; CHECK-SAME: coefficient = 5.000000e+00 : f32
; CHECK-SAME: -> !geomalg.blade<1>
; CHECK-NEXT: [[E2:%[0-9]+]] = "geomalg.blade"()
; CHECK-SAME: coefficient = 1.550000e+01 : f32
; CHECK-SAME: -> !geomalg.blade<2>
; CHECK-NEXT: [[E3:%[0-9]+]] = "geomalg.blade"()
; CHECK-SAME: coefficient = 2.560000e+01 : f32
; CHECK-SAME: -> !geomalg.blade<4>
; CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[E1]], [[E2]], [[E3]])
; CHECK-NEXT: [[CALL0:%[0-9]+]] = "geomalg.call"([[SUM0]])
; CHECK-NEXT: geomalg.return [[CALL0]]
(define-func my_func5 ()
  (my_func4 (sum (e1 5) (e2 15.5) (e3 25.6))))
(write (module-lookup geomalg-current-module "my_func5"))
(newline)

; CHECK: func.func @my_func6() -> !geomalg.unknown
; CHECK-NEXT: [[CALL0:%[0-9]+]] = "geomalg.call"()
; CHECK-NEXT: geomalg.return [[CALL0]]
(define-func my_func6 ()
  (my_func5))
(write (module-lookup geomalg-current-module "my_func6"))
(newline)

(unless (verify geomalg-current-module)
  (error "module failed verification"))
