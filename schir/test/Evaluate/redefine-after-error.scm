; RUN: not schir-scheme --mode=repl --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

; CHECK: redefine-after-error.scm:[[@LINE+1]]:13: error: boom
(define foo (error "boom"))

; CHECK: 42
(define foo 42)
foo

(define-syntax define-two
  (syntax-rules ()
    ((define-two a b e)
     (begin
       (define a e)
       (define b 1)))))

; CHECK: redefine-after-error.scm:[[@LINE+1]]:21: error: boom
(define-two bar baz (error "boom"))

; CHECK: 5
(define baz 5)
baz

; CHECK-NEXT: 6
(set! bar 6)
bar

; CHECK: redefine-after-error.scm:[[@LINE+1]]:22: error: invalid arity
(define-values (x y) (values 1 2 3))

; CHECK: (1 2)
(define-values (x y) (values 1 2))
(list x y)
