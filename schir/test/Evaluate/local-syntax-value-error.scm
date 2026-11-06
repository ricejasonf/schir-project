; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: local-syntax-value-error.scm:[[@LINE+3]]:{{[0-9]+}}: error: syntax keyword used as a value: m
(define (f)
  (define-syntax m (syntax-rules () ((_) 1)))
  m)
