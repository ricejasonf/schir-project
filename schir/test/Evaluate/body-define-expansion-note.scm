; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

;; Keep the notes of the expansion for an error in a define that a body expands.
(define-syntax bad-define
  (syntax-rules ()
    ((_) (define 5))))

; CHECK: body-define-expansion-note.scm:[[@LINE-2]]:10: error: invalid syntax for define
; CHECK: body-define-expansion-note.scm:[[@LINE+2]]:1: note: while expanding syntax: define
; CHECK: body-define-expansion-note.scm:[[@LINE+1]]:13: note: while expanding syntax: bad-define
(define (f) (bad-define) 1)
