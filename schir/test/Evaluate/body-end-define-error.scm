; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: body-end-define-error.scm:[[@LINE+3]]:3: error: body must end with an expression
(define (f)
  (if #f #f)
  (define x 1))
