; RUN: schir-scheme %s | FileCheck %s

(import (schir builtins))

; CHECK: 17
(write (+ (+ (+ 2 3) (+ 5 7))))
(newline)
