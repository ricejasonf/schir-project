; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

(define-syntax m
  (syntax-rules ()
    ((_) 1)))

; CHECK: body-shadow-keyword-error.scm:[[@LINE+3]]:{{[0-9]+}}: error: identifier is used before its definition in the body: m
; CHECK: body-shadow-keyword-error.scm:[[@LINE+3]]:{{[0-9]+}}: note: defined here
(define (f)
  (m)
  (define m 2)
  m)
