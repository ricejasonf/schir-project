; RUN: not schir-scheme -I %schir_module_path -I %geomalg_module_path %s 2>&1 | FileCheck %s

(import (schir base)
        (schir mlir)
        (geomalg base))

(with-metric 'cga)

(define-func callee ((a : !e1))
  a)

; Report call errors at the call syntax.
; CHECK: call-errors.scm:[[@LINE+3]]:13: error: failed to apply metric to function: caller
; CHECK: call-errors.scm:[[@LINE+3]]:3: note: call arguments do not match callee arity
; CHECK-NEXT: (call callee a a))
(define-func caller ((a : !e1))
  (call callee a a))
