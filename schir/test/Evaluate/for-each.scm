; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: 1
; CHECK-NEXT: 2
; CHECK-NEXT: 3
(for-each (lambda (x) (write x) (newline)) '(1 2 3))

; Do nothing for an empty list.
; CHECK-NEXT: done
(for-each (lambda (x) (write x) (newline)) '())
(write 'done)
(newline)

; Stop at the end of the shortest list.
; CHECK-NEXT: (1 a "x")
; CHECK-NEXT: (2 b "y")
; CHECK-NEXT: end
(for-each (lambda (x y z) (write (list x y z)) (newline))
          '(1 2 3)
          '(a b)
          '("x" "y" "z"))
(write 'end)
(newline)

; Apply the procedure in order for side effects.
; CHECK-NEXT: (3 2 1)
(define Acc '())
(for-each (lambda (x) (set! Acc (cons x Acc))) '(1 2 3))
(write Acc)
(newline)

; Loop over a long list without exhausting the stack.
; CHECK-NEXT: 100000
(define Count 0)
(for-each (lambda (x) (set! Count (+ Count 1))) (make-list 100000 0))
(write Count)
(newline)
