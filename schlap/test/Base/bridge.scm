; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
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

;; State each property once over a domain D and an operation f.
;; The checker takes a finite set and a Scheme procedure, the kernel a
;; type and a definition.
(define (RightIdentity D f)
  (∀ ((a ∈ D)) {(f a 0) = a}))

(define (LeftIdentity D f)
  (∀ ((a ∈ D)) {(f 0 a) = a}))

(define (Idempotent D f)
  (∀ ((a ∈ D)) {(f a a) = a}))

;; Check the properties of + on the numbers a counter has reached.
(define (Plus a b) {a + b})

(define-spec Counter
  (variables n)
  (init {n = 0})
  (next {n < 5 ∧ (′ n) = {n + 1}})
  (invariant RightIdentityUpToN (RightIdentity {0 ‥ n} Plus))
  (invariant LeftIdentityUpToN (LeftIdentity {0 ‥ n} Plus)))

; CHECK: 6
(write (check Counter 'deadlock #f))
(newline)

;; Prove the same properties of add for every natural number.
(define-definition (add (m : ℕ) (n : ℕ)) : ℕ
  (ℕ-rec (λ (k) ℕ) m (λ (k r) (succ r)) n))

(define-theorem add-right-identity
  (RightIdentity ℕ add)
  (λ (a) refl))

(define-theorem add-left-identity
  (LeftIdentity ℕ add)
  (λ (a)
    (ℕ-rec (λ (k) {(add 0 k) = k})
           refl
           (λ (k ih) (subst ih (λ (x) {(succ (add 0 k)) = (succ x)}) refl))
           a)))

; CHECK-NEXT: ()
(write (theorem-axioms 'add-left-identity))
(newline)

;; Read an integer as the natural number it denotes.
(define-theorem two-plus-two {(add 2 2) = 4} refl)

;; Evaluate excluded middle in the checker and assume it in the kernel.
(define (ExcludedMiddle D)
  (∀ ((P ∈ D)) {P ∨ (¬ P)}))

(define-spec Bit
  (variables b)
  (init {b = ⊥})
  (next {(′ b) = (¬ b)})
  (invariant ExcludedMiddleOfBits (ExcludedMiddle (set-of b (¬ b)))))

; CHECK-NEXT: 2
(write (check Bit))
(newline)

(define-axiom em (ExcludedMiddle Prop))

(define-theorem em-again (ExcludedMiddle Prop) em)

; CHECK-NEXT: (em)
(write (theorem-axioms 'em-again))
(newline)

;; Refute a false property in both.

; CHECK-NEXT: "check failed for spec: CounterIdempotent"
; CHECK-NEXT: "invariant IdempotentUpToN is violated after 1 step"
; CHECK-NEXT: "initial state: n = 0"
; CHECK-NEXT: "step 1: n = 1"
(write-error (lambda ()
  (define-spec CounterIdempotent
    (variables n)
    (init {n = 0})
    (next {n < 5 ∧ (′ n) = {n + 1}})
    (invariant IdempotentUpToN (Idempotent {0 ‥ n} Plus)))
  (check CounterIdempotent 'deadlock #f)))

; CHECK-NEXT: "declaration does not check: add-idempotent"
; CHECK-NEXT: "refl cannot prove 'add a a = a' because its sides compute to 'ℕ-rec (λ k, ℕ) a (λ k, λ r, succ r) a' and 'a'"
(write-error (lambda ()
  (define-theorem add-idempotent
    (Idempotent ℕ add)
    (λ (a) refl))
  #t))

;; Reject the arguments of the other use.

; CHECK-NEXT: "declaration does not check: over-a-set"
; CHECK-NEXT: "'schlap.range' is not a term of the proof kernel"
(write-error (lambda ()
  (define-theorem over-a-set
    (RightIdentity {0 ‥ 5} add)
    (λ (a) refl))
  #t))

; CHECK-NEXT: "declaration does not check: with-plus"
; CHECK-NEXT: "'schlap.add' is not a term of the proof kernel"
(write-error (lambda ()
  (define-theorem with-plus
    (RightIdentity ℕ Plus)
    (λ (a) refl))
  #t))

; CHECK-NEXT: "check failed for spec: OverATypeSpec"
; CHECK-NEXT: "unable to evaluate schlap.nat"
; CHECK-NEXT: "initial state: n = 0"
(write-error (lambda ()
  (define-spec OverATypeSpec
    (variables n)
    (init {n = 0})
    (next (unchanged n))
    (invariant OverAType (RightIdentity ℕ Plus)))
  (check OverATypeSpec)))

; CHECK-NEXT: "add is usable only in a theorem, definition or axiom"
(write-error (lambda ()
  (define-spec WithAddSpec
    (variables n)
    (init {n = 0})
    (next (unchanged n))
    (invariant WithAdd (RightIdentity {0 ‥ n} add)))
  (check WithAddSpec)))
