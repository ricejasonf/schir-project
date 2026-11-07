; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

;; An error in a form that a body deferred keeps the notes of its expansion.
(define-syntax wrap
  (syntax-rules ()
    ((_ e) (begin (if #f #f) e))))

; CHECK: body-expansion-note.scm:[[@LINE+5]]:11: error: invalid syntax for define
; CHECK: body-expansion-note.scm:[[@LINE+2]]:1: note: while expanding syntax: define
; CHECK: body-expansion-note.scm:[[@LINE+2]]:3: note: while expanding syntax: wrap
(define (f)
  (wrap (lambda ()
          (define 5)
          1)))
