; RUN: not schir-scheme --mode=repl --module-path=%schir_module_path %s 2>&1 \
; RUN:   | FileCheck %s
(import (schir base))

; CHECK: redefine-after-error.scm:[[@LINE+1]]:13: error: boom
(define foo (error "boom"))

; CHECK: 42
(define foo 42)
foo
