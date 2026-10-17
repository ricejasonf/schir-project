; RUN: not schir-scheme --mode=read %s 2>&1 | FileCheck %s
; Curly-infix lists require the #!curly-infix marker.

; CHECK: curly-infix is not enabled
{a + b}
