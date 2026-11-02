; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: error: boom
; CHECK: error: in after
; CHECK: "inner-after"
; CHECK-NEXT: "outer-after"
(dynamic-wind
  (lambda () #f)
  (lambda ()
    (dynamic-wind
      (lambda () #f)
      (lambda () (error "boom"))
      (lambda ()
        (write "inner-after")
        (newline)
        (error "in after"))))
  (lambda ()
    (write "outer-after")
    (newline)))
