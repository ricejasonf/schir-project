; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define a 'b)

; CHECK: 2
(write (case 'b ((a) 1) ((b c) 2) (else 3)))
(newline)

; CHECK: 3
(write (case 'd ((a) 1) ((b c) 2) (else 3)))
(newline)

; CHECK: big
(write (case (* 2 3) ((2 3 5 7) 'prime) ((1 4 6 8 9) 'big)))
(newline)

; CHECK: #\v
(write (case #\v ((#\a #\e) 'vowel) ((#\v #\w) => (lambda (x) x))))
(newline)

; CHECK: (other z)
(write (case 'z ((a) 1) (else => (lambda (x) (list 'other x)))))
(newline)

; Do not evaluate atoms as variables.
; CHECK: matched-b
(write (case 'b ((a) 'matched-a) ((b) 'matched-b)))
(newline)
