; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

; CHECK: body-shadow-literal-error.scm:[[@LINE+4]]:{{[0-9]+}}: error: identifier is used before its definition in the body: else
; CHECK: body-shadow-literal-error.scm:[[@LINE+4]]:{{[0-9]+}}: note: defined here
(define (f)
  (cond
    (else 1))
  (define else 2)
  3)
