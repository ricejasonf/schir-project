; RUN: schir-scheme -I=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: -1
(write
  (call-with-values * -))
(newline)

; CHECK: nothing
(call-with-values
  (lambda () (values))
  (lambda () (write 'nothing)))
(newline)

; CHECK-NEXT: 4
(write
  (call-with-values
    (lambda () (values 4 5))
    (lambda (a b) a)))
(newline)

; CHECK-NEXT: 5
(write
  (call-with-values
    (lambda () (values 4 5))
    (lambda (a b) b)))
(newline)

; CHECK-NEXT: 32
(write
  (call-with-values
    (lambda () (values 1 1 2 4 8 16))
    +))
(newline)

; CHECK-NEXT: (1 2 3 4 (5 6) (7 8 9) ())
((lambda ()
  (define-values ()
      (values))
  (define-values (x)
      (values 1))
  (define-values (y z)
      (values 2 3))
  (define-values (u . List1)
      (values 4 5 6))
  (define-values List2
      (values 7 8 9))
  (define-values EmptyList
      (values))
  (write (list x y z u List1 List2 EmptyList))
  (newline)
  ))
(newline)
