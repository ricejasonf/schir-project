; RUN: schir-scheme --mode=repl --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: -1
(call-with-values * -)

; CHECK-NEXT: nothing
(call-with-values
  (lambda () (values))
  (lambda () 'nothing))

; CHECK-NEXT: 4
(call-with-values
  (lambda () (values 4 5))
  (lambda (a b) a))

; CHECK-NEXT: 5
(call-with-values
  (lambda () (values 4 5))
  (lambda (a b) b))

; CHECK-NEXT: 32
(call-with-values
  (lambda () (values 1 1 2 4 8 16))
  +)

; CHECK-NEXT: (1 2 3 4 (5 6) () (7 8 9) ())
((lambda ()
  (define-values ()
      (values))
  (define-values (x)
      (values 1))
  (define-values (y z)
      (values 2 3))
  (define-values (u . List1)
      (values 4 5 6))
  (define-values (v . List2)
      (values 4))
  (define-values List3
      (values 7 8 9))
  (define-values EmptyList
      (values))
  (list x y z u List1 List2 List3 EmptyList)))
