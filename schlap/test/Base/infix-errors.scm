; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>/dev/null | FileCheck %s
; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>&1 >/dev/null | FileCheck %s --check-prefix=LOC
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap core)
        (schlap infix))

;; Write the message of the error that Thunk raises and its location.
(define (write-error Thunk)
  (guard (E (#t (write (error-object-message E))
                (newline)
                (dump-source-loc E)))
    (Thunk)
    (write "no error")
    (newline)))

; CHECK: "∧ and ∨ cannot be mixed without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∧ 2 ∨ 3}))

; CHECK-NEXT: "∧ and ∨ cannot be mixed without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∨ 2 ∨ 3 ∧ 4 ⇒ 5}))

; CHECK-NEXT: "∪ and ∩ cannot be mixed without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∪ 2 ∩ 3}))

; CHECK-NEXT: "∪ and ∩ cannot be mixed without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∈ 1 ∩ 2 ∪ 3}))

; CHECK-NEXT: "comparisons do not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 < 2 < 3}))

; CHECK-NEXT: "comparisons do not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 < 2 ≤ 3 ∧ 4}))

; CHECK-NEXT: "comparisons do not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∈ 2 ⊆ 3}))

; CHECK-NEXT: "⇔ does not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ⇔ 2 ⇒ 3 ⇔ 4}))

; CHECK-NEXT: "‥ does not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ‥ 2 + 3 ‥ 4}))

; CHECK-NEXT: "curly-infix expression is missing an operand"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 + * 2}))

; CHECK-NEXT: "curly-infix expression is missing an operand"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∧ 2 ⇒ ⇒ 3}))

; CHECK-NEXT: "curly-infix expression is missing an operator"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 ∧ 2 3}))

; CHECK-NEXT: "curly-infix expression is missing an operator"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () {1 2 ∧ 3}))

; CHECK-NEXT: "¬ takes one operand"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () (¬ 1 2)))

; CHECK-NEXT: "comparisons do not chain without braces"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () (= 1 2 3)))

; CHECK-NEXT: "⇔ takes two operands"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () (⇔ 1)))

; CHECK-NEXT: "∧ is missing an operand"
; LOC: infix-errors.scm:[[@LINE+1]]:
(write-error (lambda () (∧)))

; Report the operator on its own line.
; CHECK-NEXT: "∧ and ∨ cannot be mixed without braces"
; LOC: infix-errors.scm:[[@LINE+3]]:
(write-error (lambda ()
  {1 ∧ 2
   ∨ 3}))

; CHECK-NEXT: "no error"
(write-error (lambda () {{1 ∧ 2} ∨ {3 ∪ 4} ∩ 5 ∈ 6}))
