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

;; Test that wrong proofs of propositions are rejected.

; CHECK: "declaration does not check: swapped"
; CHECK-NEXT: "expected 'Q' but got 'P'"
(write-error (lambda ()
  (define-theorem swapped
    (∀ ((P : Prop) (Q : Prop)) {P ∧ Q ⇒ Q ∧ P})
    (λ (P Q h)
      (∧-intro (∧-left h) (∧-right h))))
  #t))

; CHECK-NEXT: "∨-elim is missing the case for the right side"
; CHECK-NEXT: "error raised here:"
(write-error (lambda ()
  (define-theorem missing-case
    (∀ ((P : Prop) (Q : Prop)) {P ∨ Q ⇒ Q ∨ P})
    (λ (P Q h)
      (∨-elim h (λ (hp) (∨-inr hp)))))
  #t))

; CHECK-NEXT: "declaration does not check: wrong-case"
; CHECK-NEXT: "expected 'P' but got 'Q'"
(write-error (lambda ()
  (define-theorem wrong-case
    (∀ ((P : Prop) (Q : Prop)) {P ∨ Q ⇒ Q ∨ P})
    (λ (P Q h)
      (∨-elim h (λ (hp) (∨-inr hp)) (λ (hq) (∨-inr hq)))))
  #t))

; CHECK-NEXT: "expecting a term: #<Undefined{{.*}}>"
; CHECK-NEXT: "error raised here:"
(write-error (lambda ()
  (define-theorem unbound-hypothesis
    (∀ ((P : Prop) (Q : Prop)) {P ⇒ Q ⇒ P})
    (λ (P Q hp hq) hr))
  #t))

; CHECK-NEXT: "declaration does not check: extra-hypothesis"
; CHECK-NEXT: "λ binds a variable but 'Q ∧ P' is expected, which is not a ∀ or ⇒"
(write-error (lambda ()
  (define-theorem extra-hypothesis
    (∀ ((P : Prop) (Q : Prop)) {P ∧ Q ⇒ Q ∧ P})
    (λ (P Q h extra)
      (∧-intro (∧-right h) (∧-left h))))
  #t))

; CHECK-NEXT: "declaration does not check: not-a-conjunction"
; CHECK-NEXT: "∧-left expects a proof of a conjunction but got a proof of 'P ∨ Q'"
(write-error (lambda ()
  (define-theorem not-a-conjunction
    (∀ ((P : Prop) (Q : Prop)) {P ∨ Q ⇒ P})
    (λ (P Q h) (∧-left h)))
  #t))

; CHECK-NEXT: "declaration does not check: not-a-disjunction"
; CHECK-NEXT: "∨-inl proves a disjunction but 'P ∧ Q' is expected"
(write-error (lambda ()
  (define-theorem not-a-disjunction
    (∀ ((P : Prop) (Q : Prop)) {P ⇒ P ∧ Q})
    (λ (P Q h) (∨-inl h)))
  #t))

; CHECK-NEXT: "declaration does not check: applied-lambda"
; CHECK-NEXT: "cannot infer the type of λ here; use it where a ∀ or ⇒ is expected"
(write-error (lambda ()
  (define-theorem applied-lambda
    (∀ ((P : Prop)) {P ⇒ P})
    (λ (P h) (app (λ (x) x) h)))
  #t))

; CHECK-NEXT: "declaration does not check: not-a-proposition"
; CHECK-NEXT: "the statement 'Prop' is not a proposition"
(write-error (lambda ()
  (define-theorem not-a-proposition Prop ⊤-intro)
  #t))

;; Test that ⊥ cannot be proved without an axiom.

; CHECK-NEXT: "declaration does not check: false-by-lambda"
; CHECK-NEXT: "λ binds a variable but '⊥' is expected, which is not a ∀ or ⇒"
(write-error (lambda ()
  (define-theorem false-by-lambda ⊥ (λ (h) h))
  #t))

; CHECK-NEXT: "declaration does not check: false-by-argument"
; CHECK-NEXT: "expected 'P' but got 'Prop'"
(write-error (lambda ()
  (define-theorem false-by-argument
    (∀ ((P : Prop)) P)
    (λ (P) P))
  #t))

; CHECK-NEXT: "declaration does not check: false-by-truth"
; CHECK-NEXT: "expected '⊥' but got '⊤'"
(write-error (lambda ()
  (define-theorem false-by-truth ⊥ ⊤-intro)
  #t))

; CHECK-NEXT: "declaration does not check: false-by-elim"
; CHECK-NEXT: "cannot infer what ⊥-elim proves here; use it where its proposition is expected"
(write-error (lambda ()
  (define-theorem false-by-elim ⊥ (app (⊥-elim ⊤-intro) ⊤-intro))
  #t))

;; Test that a theorem cannot be redefined to use itself.

(define-theorem self ⊤ ⊤-intro)

(define (use-self . Args) (apply self Args))

; CHECK-NEXT: "declaration does not check: self"
; CHECK-NEXT: "redefinition of 'self'"
(write-error (lambda ()
  (define-theorem self ⊥ (use-self))
  #t))

;; Test that a declaration is not applied outside a declaration.

; CHECK-NEXT: "self is usable only in a theorem, definition or axiom"
(write-error (lambda () (self ⊤-intro)))

;; Test that rejected theorems are removed from the module.

; CHECK-NEXT: #op{module {
; CHECK-NEXT: schlap.theorem @self attributes {axioms = [], checked} {
; CHECK-NOT: schlap.theorem
(write schlap-current-module)
(newline)
