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

(define ModulePrefix
  (mangle-module '_LOL '(LOL base)))
(define (test-mangle-func FuncName)
  (write (mangle-function-name ModulePrefix FuncName))
  (newline))

; CHECK: _LOLL3SLOLL4SbaseF2Sid
(test-mangle-func "id")

; CHECK: _LOLL3SLOLL4SbaseF3Srunmi4Sfunc
(test-mangle-func "run-func")

; CHECK: _LOLL3SLOLL4SbaseF13Ssnake_func_fn
(test-mangle-func "snake_func_fn")

; CHECK: _LOLL3SLOLL4SbaseF10SPascalFunc
(test-mangle-func "PascalFunc")

; CHECK: _LOLL3SLOLL4SbaseF3Sdotdt4Sfuncdt2Sfn
(test-mangle-func "dot.func.fn")
