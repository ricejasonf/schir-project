; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: 42
(write (let* () 42))
(newline)

; CHECK: (1 2 3)
(write (let* ((x 1) (y (+ x 1)) (z (+ y 1))) (list x y z)))
(newline)

; Shadow an outer binding with a later init that sees the earlier one.
; CHECK: 20
(write (let ((x 1))
         (let* ((x 10) (y (* x 2))) y)))
(newline)

; Allow internal defines in the body.
; CHECK: 6
(write (let* ((x 2))
         (define y 3)
         (* x y)))
(newline)
