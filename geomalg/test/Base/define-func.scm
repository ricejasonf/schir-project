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
(define-func my_func1 ((arg0 : e1)
                       (arg1 : e2)
                       (arg2 : (blade-type e1 e2))
                       (arg3 : (blade-type e2 e1)))
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
(define-func my_func2 ((arg0 : (blade-type e1 e2 e3))
                       (arg1 : (blade-type e3 e2 e1)))
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
(define-func my_func3 ((arg0 : (blade-type e1 e2 e3 ni))
                       (arg1 : (blade-type ni e3 e2 e1)))
  (sum arg0
       arg1
       ))
(write (module-lookup geomalg-current-module "my_func3"))
(newline)

; CHECK: func.func @my_func4([[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <3>>) -> !geomalg.unknown {
(define-func my_func4 ((arg0 : (multivector-type e1 e2 (blade-type e1 e2))))
  ; FIXME should be able to just return arg0 but it is not the !schir.unknown type.
  ;       (This can be fixed in the type inference pass.)
  (sum arg0))
(write (module-lookup geomalg-current-module "my_func4"))
(newline)

(unless (verify geomalg-current-module)
  (error "module failed verification"))
