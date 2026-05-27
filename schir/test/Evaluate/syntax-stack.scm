; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

(define-syntax foo
  (syntax-rules ()
    ((foo x )
      (lambda ()
        (dump 5)
        (lambda ()
          (define 5))))))

(foo 5)
; CHECK: syntax-stack.scm:10:11: error: invalid syntax for define
; CHECK: syntax-stack.scm:12:1: note: while expanding syntax: foo
; CHECK: syntax-stack.scm:7:7: note: while expanding syntax: lambda
; CHECK: syntax-stack.scm:9:9: note: while expanding syntax: lambda
; CHECK: syntax-stack.scm:10:11: note: while expanding syntax: define
