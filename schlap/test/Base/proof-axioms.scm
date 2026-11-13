; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap core)
        (schlap proof)
        (schlap infix))

;; Write the message of the error Thunk raises and of each note.
(define (write-error Thunk)
  (guard (E (#t (write (error-object-message E))
                (newline)
                (for-each
                  (lambda (I)
                    (when (error-object? I)
                      (write (error-object-message I))
                      (newline)))
                  (error-object-irritants E))))
    (Thunk)
    (write "no error")
    (newline)))

;; Test classical theorems that list the axioms they depend on.

(define-axiom excluded-middle
  (∀ ((P : Prop)) {P ∨ (¬ P)}))

(define-theorem double-negation
  (∀ ((P : Prop)) {(¬ (¬ P)) ⇒ P})
  (λ (P h)
    (∨-elim (excluded-middle P)
            (λ (hp) hp)
            (λ (hnp) (⊥-elim (app h hnp))))))

(define-theorem not-and
  (∀ ((P : Prop) (Q : Prop)) {(¬ {P ∧ Q}) ⇒ (¬ P) ∨ (¬ Q)})
  (λ (P Q h)
    (∨-elim (excluded-middle P)
            (λ (hp) (∨-inr (λ (hq) (app h (∧-intro hp hq)))))
            (λ (hnp) (∨-inl hnp)))))

;; Depend on an axiom through another theorem.
(define-theorem stable-excluded-middle
  (∀ ((P : Prop)) {(¬ (¬ {P ∨ (¬ P)})) ⇒ {P ∨ (¬ P)}})
  (λ (P) (double-negation {P ∨ (¬ P)})))

(define-axiom anything (∀ ((P : Prop)) P))

(define-theorem false-from-axiom ⊥ (anything ⊥))

(define-theorem constructive
  (∀ ((P : Prop)) {P ⇒ (¬ (¬ P))})
  (λ (P hp hnp) (app hnp hp)))

; CHECK: (excluded-middle)
(write (theorem-axioms 'double-negation))
(newline)

; CHECK-NEXT: (excluded-middle)
(write (theorem-axioms 'not-and))
(newline)

; CHECK-NEXT: (excluded-middle)
(write (theorem-axioms 'stable-excluded-middle))
(newline)

; CHECK-NEXT: (anything)
(write (theorem-axioms 'false-from-axiom))
(newline)

; CHECK-NEXT: ()
(write (theorem-axioms 'constructive))
(newline)

; CHECK-NEXT: "declaration does not check: not-a-proposition"
; CHECK-NEXT: "the statement 'ℕ' is not a proposition"
(write-error (lambda ()
  (define-axiom not-a-proposition ℕ)
  #t))

; CHECK: schlap.axiom @"excluded-middle" attributes {checked} {
; CHECK: schlap.theorem @"double-negation" attributes {axioms = [@"excluded-middle"], checked} {
; CHECK: schlap.theorem @"stable-excluded-middle" attributes {axioms = [@"excluded-middle"], checked} {
; CHECK: schlap.theorem @"false-from-axiom" attributes {axioms = [@anything], checked} {
(write schlap-current-module)
(newline)
