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

;; Test definitions, computation and induction on ℕ.

(define-definition (add (m : ℕ) (n : ℕ)) : ℕ
  (ℕ-rec (λ (k) ℕ) m (λ (k r) (succ r)) n))

(define-definition two : ℕ (succ (succ zero)))

(define-theorem add-zero
  (∀ ((n : ℕ)) {(add n zero) = n})
  (λ (n) refl))

(define-theorem two-plus-two
  {(add two two) = (succ (succ (succ (succ zero))))}
  refl)

(define-theorem zero-add
  (∀ ((n : ℕ)) {(add zero n) = n})
  (λ (n)
    (ℕ-rec (λ (k) {(add zero k) = k})
           refl
           (λ (k ih) (subst ih (λ (x) {(succ (add zero k)) = (succ x)}) refl))
           n)))

;; State a theorem with a procedure that takes a definition.
(define (RightIdentity f e)
  (∀ ((a : ℕ)) {(f a e) = a}))

(define-theorem add-right-identity
  (RightIdentity add zero)
  (λ (a) refl))

;; Eliminate into Prop to tell zero from a successor.
(define-definition (is-zero (n : ℕ)) : Prop
  (ℕ-rec (λ (k) Prop) ⊤ (λ (k r) ⊥) n))

(define-theorem zero-≠-one
  {zero ≠ (succ zero)}
  (λ (h) (subst h (λ (x) (is-zero x)) ⊤-intro)))

; CHECK-LABEL: schlap.definition @add attributes {axioms = [], checked} : {
; CHECK: } = {
; CHECK: schlap.nat_rec %{{.+}}, %{{.+}}, %{{.+}}, %{{.+}} : (!schlap.term, !schlap.nat, !schlap.term, !schlap.nat) -> !schlap.nat
; CHECK-LABEL: schlap.theorem @"zero-add" attributes {axioms = [], checked} {
; CHECK: } proof {
; CHECK: schlap.nat_rec %{{.+}}, %{{.+}}, %{{.+}}, %{{.+}} : (!schlap.term, !schlap.proof, !schlap.proof, !schlap.nat) -> !schlap.proof
(write schlap-current-module)
(newline)

;; Test that wrong uses of ℕ are rejected.

; CHECK: "declaration does not check: wrong-step"
; CHECK-NEXT: "expected 'add zero (succ k) = succ k' but got 'add zero k = k'"
(write-error (lambda ()
  (define-theorem wrong-step
    (∀ ((n : ℕ)) {(add zero n) = n})
    (λ (n)
      (ℕ-rec (λ (k) {(add zero k) = k})
             refl
             (λ (k ih) ih)
             n)))
  #t))

; CHECK-NEXT: "declaration does not check: false-by-induction"
; CHECK-NEXT: "expected 'is-zero (succ k)' but got 'is-zero k'"
(write-error (lambda ()
  (define-theorem false-by-induction
    (∀ ((n : ℕ)) (is-zero n))
    (λ (n) (ℕ-rec (λ (k) (is-zero k)) ⊤-intro (λ (k ih) ih) n)))
  #t))

; CHECK-NEXT: "declaration does not check: one-is-zero"
; CHECK-NEXT: "expected 'is-zero (succ zero)' but got '⊤'"
(write-error (lambda ()
  (define-theorem one-is-zero (is-zero (succ zero)) ⊤-intro)
  #t))

; CHECK-NEXT: "declaration does not check: add-not-commuting"
; CHECK-NEXT: "refl cannot prove 'add zero n = n' because its sides compute to 'ℕ-rec (λ k, ℕ) zero (λ k, λ r, succ r) n' and 'n'"
(write-error (lambda ()
  (define-theorem add-not-commuting
    (∀ ((n : ℕ)) {(add zero n) = n})
    (λ (n) refl))
  #t))

; CHECK-NEXT: "declaration does not check: bad-definition"
; CHECK-NEXT: "expected 'ℕ' but got '⊤'"
(write-error (lambda ()
  (define-definition bad-definition : ℕ ⊤-intro)
  #t))

; CHECK-NEXT: "declaration does not check: bad-motive"
; CHECK-NEXT: "expected a type but 'zero' has type 'ℕ'"
(write-error (lambda ()
  (define-definition bad-motive : ℕ
    (ℕ-rec (λ (k) zero) zero (λ (k r) r) zero))
  #t))

;; Stop normalising a term whose normal form is too large.
(define-definition (double (n : ℕ)) : ℕ
  (ℕ-rec (λ (k) ℕ) zero (λ (k r) (succ (succ r))) n))

(define-definition (pow2 (n : ℕ)) : ℕ
  (ℕ-rec (λ (k) ℕ) (succ zero) (λ (k r) (double r)) n))

(define-definition (times4 (n : ℕ)) : ℕ (double (double n)))

(define-definition n20 : ℕ
  (times4 (succ (succ (succ (succ (succ zero)))))))

; CHECK-NEXT: "declaration does not check: too-large"
; CHECK-NEXT: "'pow2 n20' does not normalize within the limit"
(write-error (lambda ()
  (define-theorem too-large {(pow2 n20) = zero} refl)
  #t))

;; Compare terms before normalising them.
(define-theorem same-large {(pow2 n20) = (pow2 n20)} refl)
