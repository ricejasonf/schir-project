; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

; CHECK: body-shadow-template-error.scm:[[@LINE+4]]:{{[0-9]+}}: error: identifier is used before its definition in the body: foo
; CHECK: body-shadow-template-error.scm:[[@LINE+5]]:{{[0-9]+}}: note: defined here
(define (f)
  (define-syntax m
    (syntax-rules () ((_) (foo 1))))
  (m)
  (define (foo x) x)
  2)
