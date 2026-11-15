; RUN: not schir-scheme --mode=repl-check --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

; CHECK: define-values-arity-error.scm:[[@LINE+1]]:22: error: invalid arity
(define-values (a b) (values 1 2 3))

; CHECK: define-values-arity-error.scm:[[@LINE+1]]:24: error: invalid arity
(define-values (c d e) (values 1 2))

; CHECK: define-values-arity-error.scm:[[@LINE+1]]:20: error: invalid arity
(define-values (f) (values 1 2))

; CHECK: define-values-arity-error.scm:[[@LINE+1]]:19: error: invalid arity
(define-values () (values 1))

; CHECK: define-values-arity-error.scm:[[@LINE+1]]:26: error: invalid arity
(define-values (g h . r) (values 1))

; CHECK: (1 2)
(define-values (x y) (values 1 2))
(list x y)
