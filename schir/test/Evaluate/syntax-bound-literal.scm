; RUN: schir-scheme --module-path=%schir_module_path --module-path=%S/Inputs %s | FileCheck %s
(import (schir base)
        (my literals))

; Literals bound in a library match the same binding at the use site.
; CHECK: syntax-matched
(write (match-literals 1 ⌋ 2))
(newline)
; CHECK-NEXT: variable-matched
(write (match-literals 1 my-var 2))
(newline)

; CHECK-NEXT: syntax-matched
(define (f) (match-literals 1 ⌋ 2))
(write (f))
(newline)

; A shadowed literal does not match.
; CHECK-NEXT: no-match
(let ((⌋ 1))
  (write (match-literals 1 ⌋ 2)))
(newline)

; Literals passed through another macro keep their binding.
; CHECK-NEXT: syntax-matched
(define-syntax pass-op
  (syntax-rules ()
    ((pass-op op) (match-literals 1 op 2))))
(write (pass-op ⌋))
(newline)

; CHECK-NEXT: syntax-matched
(define-syntax insert-op
  (syntax-rules ()
    ((insert-op a b) (match-literals a ⌋ b))))
(write (insert-op 1 2))
(newline)

; Literals bound in the same file.
(define-syntax ∧
  (syntax-rules ()
    ((∧ a b) (list a b))))
(define-syntax match-wedge
  (syntax-rules (∧)
    ((match-wedge a ∧ b) 'wedge-matched)
    ((match-wedge x ...) 'no-match)))

; CHECK-NEXT: wedge-matched
(write (match-wedge 1 ∧ 2))
(newline)
; CHECK-NEXT: no-match
(write (match-wedge 1 ⌋ 2))
(newline)
