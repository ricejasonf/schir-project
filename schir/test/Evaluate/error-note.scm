; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 | FileCheck %s
(import (schir builtins))

(define foo 'foo)

; CHECK: error-note.scm:8:1: error: invalid object: foo
; CHECK: error-note.scm:4:13: note: defined here
(error "invalid object: {}"
       foo
       (error-note "defined here" foo))

