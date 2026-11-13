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

;; Test quantifiers over a type and equality.

(define-theorem =-symm
  (∀ ((a : ℕ) (b : ℕ)) {{a = b} ⇒ {b = a}})
  (λ (a b h) (subst h (λ (x) {x = a}) refl)))

(define-theorem =-trans
  (∀ ((a : ℕ) (b : ℕ) (c : ℕ)) {{a = b} ⇒ {b = c} ⇒ {a = c}})
  (λ (a b c hab hbc) (subst hbc (λ (x) {a = x}) hab)))

(define-theorem ∃-weaken
  (∀ ((P : {ℕ ⇒ Prop}) (Q : {ℕ ⇒ Prop}))
    {(∃ ((x : ℕ)) {(app P x) ∧ (app Q x)}) ⇒ (∃ ((x : ℕ)) (app P x))})
  (λ (P Q h)
    (∃-elim h (λ (x hx) (∃-intro x (∧-left hx))))))

(define-theorem ∀-instance
  (∀ ((P : {ℕ ⇒ Prop})) {(∀ ((n : ℕ)) (app P n)) ⇒ (app P zero)})
  (λ (P h) (app h zero)))

(define-theorem ∃-zero
  (∃ ((x : ℕ)) {x = zero})
  (∃-intro zero refl))

;; Quantify over all propositions with a statement that is a proposition.
(define-theorem prop-=-refl
  (∀ ((P : Prop)) {P = P})
  (λ (P) refl))

; CHECK-LABEL: schlap.theorem @"=-symm" attributes {axioms = [], checked} {
; CHECK: schlap.nat : !schlap.term
; CHECK: schlap.pi (%{{.+}}: !schlap.nat) in
; CHECK: } proof {
; CHECK: schlap.subst %{{.+}}, %{{.+}}, %{{.+}} : (!schlap.proof, !schlap.term, !schlap.proof) -> !schlap.proof
(write schlap-current-module)
(newline)

;; Test that wrong proofs with quantifiers and equality are rejected.

; CHECK: "declaration does not check: wrong-witness"
; CHECK-NEXT: "refl cannot prove 'succ zero = zero' because its sides compute to 'succ zero' and 'zero'"
(write-error (lambda ()
  (define-theorem wrong-witness
    (∃ ((x : ℕ)) {x = zero})
    (∃-intro (succ zero) refl))
  #t))

; CHECK-NEXT: "declaration does not check: false-by-refl"
; CHECK-NEXT: "refl cannot prove 'a = b' because its sides compute to 'a' and 'b'"
(write-error (lambda ()
  (define-theorem false-by-refl
    (∀ ((a : ℕ) (b : ℕ)) {a = b})
    (λ (a b) refl))
  #t))

; CHECK-NEXT: "declaration does not check: subst-backwards"
; CHECK-NEXT: "expected 'b = a' but got 'a = b'"
(write-error (lambda ()
  (define-theorem subst-backwards
    (∀ ((a : ℕ) (b : ℕ)) {{a = b} ⇒ {b = a}})
    (λ (a b h) (subst h (λ (x) {a = x}) refl)))
  #t))

; CHECK-NEXT: "declaration does not check: escaping-witness"
; CHECK-NEXT: "∃-elim proves only propositions but 'ℕ' is expected"
(write-error (lambda ()
  (define-definition escaping-witness : ℕ
    (∃-elim (∃-intro zero refl) (λ (x hx) x)))
  #t))

; CHECK-NEXT: "declaration does not check: mixed-types"
; CHECK-NEXT: "expected 'Prop' but got 'ℕ'"
(write-error (lambda ()
  (define-theorem mixed-types
    (∀ ((P : Prop)) {P = zero})
    (λ (P) refl))
  #t))
