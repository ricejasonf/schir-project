; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: error: expansion template should contain pack
(define-syntax no-pack
  (syntax-rules ()
    ((_ x y ...) '(x ...))))

(no-pack a 1 2)
