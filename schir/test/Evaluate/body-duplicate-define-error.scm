; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: body-duplicate-define-error.scm:[[@LINE+3]]:{{[0-9]+}}: error: local variable has duplicate definitions
(define (f)
  (define a 1)
  (define a 2)
  a)
