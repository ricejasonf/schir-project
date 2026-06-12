; RUN: schir-scheme %s | FileCheck %s
(import (schir builtins))

; Test top level begin.
; CHECK: foo
; CHECK-NEXT: bar
(begin
  (define global 'global)
  (write 'foo)
  (newline)
  (write 'bar)
  (newline))

; CHECK: global56
(define-syntax my-begin
  (syntax-rules ()
    ((my-begin x y)
     (begin
        (write global)
        (write x)
        (write y)
        (newline)))))
(my-begin 5 6)

; CHECK: yo11
((lambda ()
   (define local 5)
   (begin
     (define also-local 6)
     (write 'yo))
   (write (+ local also-local))))
