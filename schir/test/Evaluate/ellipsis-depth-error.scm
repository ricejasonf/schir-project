; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: ellipsis-depth-error.scm:[[@LINE+3]]:33: error: pattern variable followed by too many ellipsis
(define-syntax inner-under-two
  (syntax-rules ()
    ((_ x (k y ...) ...) '((k (x k y) ...) ...))))

(inner-under-two a (p 1 2) (q 3))
