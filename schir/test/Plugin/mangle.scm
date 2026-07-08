; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir builtins)
        (schir mangle))

(define ManglePrefix '_FOOBAR)
(define (test-mangle-module Spec)
  (write (mangle-module ManglePrefix Spec))
  (newline))

; CHECK: "_FOOBARL3Sone"
(test-mangle-module '(one))

; CHECK: "_FOOBARL5SschirL4Sbase"
(test-mangle-module '(schir base))

; CHECK: "_FOOBARL5SschirL5Sclang"
(test-mangle-module '(schir clang))

; CHECK: "_FOOBARL5SschirL4Smlir"
(test-mangle-module '(schir mlir))

; CHECK: "_FOOBARL5SschirL3Sfoomi3Sbar"
(test-mangle-module '(schir foo-bar))

; CHECK: "_FOOBARL5SschirL3Sfoodt3SbarL2S42"
(test-mangle-module '(schir foo.bar 42))

; CHECK: "_FOOBARL8Sfoo_bar3"
(test-mangle-module '(foo_bar3))

; CHECK: "_FOOBARL4SrmrfLml"
(test-mangle-module '(rmrf *))

;; TODO Check failure.
;(test-mangle-module '(foo_bar3 (woof)))

