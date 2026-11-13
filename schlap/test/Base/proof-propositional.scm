; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap core)
        (schlap proof)
        (schlap infix))

;; Test propositional theorems.

(define-theorem ∧-comm
  (∀ ((P : Prop) (Q : Prop)) {P ∧ Q ⇒ Q ∧ P})
  (λ (P Q h)
    (∧-intro (∧-right h) (∧-left h))))

(define-theorem ∨-comm
  (∀ ((P : Prop) (Q : Prop)) {P ∨ Q ⇒ Q ∨ P})
  (λ (P Q h)
    (∨-elim h (λ (hp) (∨-inr hp)) (λ (hq) (∨-inl hq)))))

(define-theorem modus-ponens
  (∀ ((P : Prop) (Q : Prop)) {P ⇒ {P ⇒ Q} ⇒ Q})
  (λ (P Q hp hpq) (app hpq hp)))

(define-theorem contraposition
  (∀ ((P : Prop) (Q : Prop)) {{P ⇒ Q} ⇒ (¬ Q) ⇒ (¬ P)})
  (λ (P Q hpq hnq hp) (app hnq (app hpq hp))))

(define-theorem de-morgan-not-or
  (∀ ((P : Prop) (Q : Prop)) {(¬ {P ∨ Q}) ⇒ (¬ P) ∧ (¬ Q)})
  (λ (P Q h)
    (∧-intro (λ (hp) (app h (∨-inl hp)))
             (λ (hq) (app h (∨-inr hq))))))

(define-theorem de-morgan-and-not
  (∀ ((P : Prop) (Q : Prop)) {(¬ P) ∧ (¬ Q) ⇒ (¬ {P ∨ Q})})
  (λ (P Q h hpq)
    (∨-elim hpq (∧-left h) (∧-right h))))

(define-theorem de-morgan-or-not
  (∀ ((P : Prop) (Q : Prop)) {(¬ P) ∨ (¬ Q) ⇒ (¬ {P ∧ Q})})
  (λ (P Q h hpq)
    (∨-elim h
            (λ (np) (app np (∧-left hpq)))
            (λ (nq) (app nq (∧-right hpq))))))

(define-theorem not-not-excluded-middle
  (∀ ((P : Prop)) (¬ (¬ {P ∨ (¬ P)})))
  (λ (P h)
    (app h (∨-inr (λ (hp) (app h (∨-inl hp)))))))

(define-theorem ex-falso
  (∀ ((P : Prop)) {⊥ ⇒ P})
  (λ (P h) (⊥-elim h)))

(define-theorem trivial ⊤ ⊤-intro)

;; Use a theorem in another proof.
(define-theorem ∧-comm-twice
  (∀ ((P : Prop) (Q : Prop)) {P ∧ Q ⇒ P ∧ Q})
  (λ (P Q h) (∧-comm Q P (∧-comm P Q h))))

; CHECK: ()
(write (theorem-axioms '∧-comm-twice))
(newline)

; CHECK-LABEL: schlap.theorem @"\E2\88\A7-comm" attributes {axioms = [], checked} {
; CHECK: %[[PROP:.+]] = schlap.prop_sort : !schlap.term
; CHECK: schlap.pi (%[[P:.+]]: !schlap.prop) in %[[PROP]] : !schlap.term -> !schlap.prop {
; CHECK: schlap.pi (%[[Q:.+]]: !schlap.prop) in %{{.+}} : !schlap.term -> !schlap.prop {
; CHECK: %[[PQ:.+]] = schlap.and %[[P]], %[[Q]] : !schlap.prop
; CHECK: %[[QP:.+]] = schlap.and %[[Q]], %[[P]] : !schlap.prop
; CHECK: schlap.pi (%{{.+}}: !schlap.proof) in %[[PQ]] : !schlap.prop -> !schlap.prop {
; CHECK-NEXT: schlap.yield %[[QP]] : !schlap.prop
; CHECK: } proof {
; CHECK: schlap.lambda {
; CHECK: ^bb0(%{{.+}}: !schlap.prop):
; CHECK: schlap.lambda {
; CHECK: ^bb0(%{{.+}}: !schlap.prop):
; CHECK: schlap.lambda {
; CHECK: ^bb0(%[[H:.+]]: !schlap.proof):
; CHECK-NEXT: %[[R:.+]] = schlap.and_right %[[H]] : !schlap.proof
; CHECK-NEXT: %[[L:.+]] = schlap.and_left %[[H]] : !schlap.proof
; CHECK-NEXT: %[[I:.+]] = schlap.and_intro %[[R]], %[[L]] : !schlap.proof
; CHECK-NEXT: schlap.yield %[[I]] : !schlap.proof

; CHECK-LABEL: schlap.theorem @"modus-ponens"
; CHECK: } proof {
; CHECK: schlap.app %{{.+}}, %{{.+}} : !schlap.proof

; CHECK-LABEL: schlap.theorem @"\E2\88\A7-comm-twice"
; CHECK: } proof {
; CHECK: schlap.ref @"\E2\88\A7-comm" : !schlap.proof
(write schlap-current-module)
(newline)

(unless (verify schlap-current-module)
  (error "module failed verification"))
