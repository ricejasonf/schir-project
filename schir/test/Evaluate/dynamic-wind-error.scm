; RUN: not schir-scheme --mode=repl-check %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: dynamic-wind-error.scm:[[@LINE+8]]:18: error: boom
; CHECK: "inner-after"
; CHECK: dynamic-wind-error.scm:[[@LINE+10]]:9: error: in after
(dynamic-wind
  (lambda () #f)
  (lambda ()
    (dynamic-wind
      (lambda () #f)
      (lambda () (error "boom"))
      (lambda ()
        (write "inner-after")
        (newline)
        (error "in after"))))
  (lambda ()
    (write "outer-after")
    (newline)))
; CHECK: "outer-after"

; Run the after thunk when escaping from an error.
(define flag #f)
; CHECK: dynamic-wind-error.scm:[[@LINE+3]]:14: error: flag
(dynamic-wind
  (lambda () (set! flag #t))
  (lambda () (error "flag"))
  (lambda () (set! flag #f)))
; CHECK: #f
flag

; Skip the after thunk when the before thunk fails on reentry.
(define reenter-k #f)
(define is-reentry #f)
(define is-after-reentry #f)
; CHECK: dynamic-wind-error.scm:[[@LINE+4]]:7: error: reentry
(dynamic-wind
  (lambda ()
    (if is-reentry
      (error "reentry")))
  (lambda () (call/cc (lambda (k) (set! reenter-k k))))
  (lambda () (if is-reentry (set! is-after-reentry #t))))
(if is-reentry #f (begin (set! is-reentry #t) (reenter-k 0)))
; CHECK: #f
is-after-reentry
