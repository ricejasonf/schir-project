; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: define-syntax-spec-error.scm:[[@LINE+1]]:{{[0-9]+}}: error: invalid syntax spec
(define-syntax no-transformer)
