; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

(define (f) 1)

; CHECK: body-shadow-call-error.scm:[[@LINE+3]]:{{[0-9]+}}: error: identifier is used before its definition in the body: f
; CHECK: body-shadow-call-error.scm:[[@LINE+3]]:{{[0-9]+}}: note: defined here
(define (g)
  (f)
  (define (f) 2)
  (f))
