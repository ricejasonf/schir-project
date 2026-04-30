; RUN: schir-scheme %s | FileCheck %s
(import (schir builtins))

; Test top level begin.
; CHECK: foo
; CHECK-NEXT: bar
(begin
  (write 'foo)
  (newline)
  (write 'bar)
  (newline))

; CHECK: 56
(define-syntax my-begin
  (syntax-rules ()
    ((my-begin x y)
     (begin
        (write x)
        (write y)
        (newline)))))
(my-begin 5 6)
