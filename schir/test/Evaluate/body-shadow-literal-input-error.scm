; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

(define-syntax q
  (syntax-rules (foo)
    ((_ foo) 'yes)
    ((_ x) 'x)))

; CHECK: body-shadow-literal-input-error.scm:[[@LINE+3]]:{{[0-9]+}}: error: identifier is used before its definition in the body: bar
; CHECK: body-shadow-literal-input-error.scm:[[@LINE+3]]:{{[0-9]+}}: note: defined here
(define (f)
  (q bar)
  (define bar 3)
  bar)
