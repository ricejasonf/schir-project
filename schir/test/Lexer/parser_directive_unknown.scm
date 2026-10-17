; RUN: not schir-scheme --mode=read %s 2>&1 | FileCheck %s

; CHECK: unsupported parser directive
#!fold-casez
