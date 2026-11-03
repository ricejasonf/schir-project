; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 | FileCheck %s
(import (schir base))

; Report an error that no guard clause matches only once.
; CHECK: error: boom
; CHECK-NOT: uncaught exception
; CHECK: "after"
; CHECK-NOT: uncaught exception
(dynamic-wind
  (lambda () #f)
  (lambda ()
    (guard (e ((string? e) 'no))
      (error "boom")))
  (lambda () (write "after") (newline)))
