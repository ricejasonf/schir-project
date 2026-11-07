; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: lambda-duplicate-formal-error.scm:[[@LINE+1]]:{{[0-9]+}}: error: duplicate parameter name
(define (f a a) a)
