; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

; CHECK-LABEL: schlap.spec @DieHard {
; CHECK-NEXT: [[SMALL:%.+]] = schlap.variable "small" : !schlap.unknown
; CHECK-NEXT: [[BIG:%.+]] = schlap.variable "big" : !schlap.unknown
; CHECK-NEXT: schlap.init {
; CHECK-NEXT: [[Z:%.+]] = schlap.int 0 : !schlap.unknown
; CHECK-NEXT: [[A:%.+]] = schlap.eq [[SMALL]], [[Z]] : !schlap.unknown
; CHECK-NEXT: [[Z2:%.+]] = schlap.int 0 : !schlap.unknown
; CHECK-NEXT: [[B:%.+]] = schlap.eq [[BIG]], [[Z2]] : !schlap.unknown
; CHECK-NEXT: [[AND:%.+]] = schlap.and [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: schlap.yield [[AND]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: schlap.action @FillSmall {
; CHECK-NEXT: [[P:%.+]] = schlap.prime [[SMALL]] : !schlap.unknown
; CHECK-NEXT: [[THREE:%.+]] = schlap.int 3 : !schlap.unknown
; CHECK-NEXT: [[E1:%.+]] = schlap.eq [[P]], [[THREE]] : !schlap.unknown
; CHECK-NEXT: [[U:%.+]] = schlap.unchanged [[BIG]] : (!schlap.unknown) -> !schlap.unknown
; CHECK-NEXT: [[AND:%.+]] = schlap.and [[E1]], [[U]] : !schlap.unknown
; CHECK-NEXT: schlap.yield [[AND]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: schlap.action @BigToSmall {
; CHECK: schlap.lt
; CHECK: schlap.ite
; CHECK: schlap.yield
; CHECK-NEXT: }
; CHECK-NEXT: schlap.next {
; CHECK-NEXT: [[R1:%.+]] = schlap.action_ref @FillSmall : !schlap.unknown
; CHECK-NEXT: [[R2:%.+]] = schlap.action_ref @BigToSmall : !schlap.unknown
; CHECK-NEXT: [[OR:%.+]] = schlap.or [[R1]], [[R2]] : !schlap.unknown
; CHECK-NEXT: schlap.yield [[OR]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: schlap.invariant @NotSolved {
; CHECK-NEXT: [[FOUR:%.+]] = schlap.int 4 : !schlap.unknown
; CHECK-NEXT: [[NE:%.+]] = schlap.ne [[BIG]], [[FOUR]] : !schlap.unknown
; CHECK-NEXT: schlap.yield [[NE]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: }
(define-spec DieHard
  (variables small big)

  (define (Min m n) (ite {m < n} m n))

  (init {small = 0 ∧ big = 0})

  (action FillSmall {(′ small) = 3 ∧ (unchanged big)})
  (action BigToSmall
    (let ((n (Min big {3 - small})))
      {(′ big) = {big - n} ∧ (′ small) = {small + n}}))

  (next {FillSmall ∨ BigToSmall})

  (invariant NotSolved {big ≠ 4}))

(write DieHard)
(newline)

;; Test the IR of the value forms.

; CHECK-LABEL: schlap.spec @Values {
; CHECK: [[F:%.+]] = schlap.variable "f" : !schlap.unknown
; CHECK: [[T:%.+]] = schlap.variable "t" : !schlap.unknown
; CHECK: schlap.init {
; CHECK: [[S:%.+]] = schlap.set_of %{{.+}}, %{{.+}} : (!schlap.unknown, !schlap.unknown) -> !schlap.unknown
; CHECK: schlap.fn ({{%.+}}: !schlap.unknown) in [[S]] : !schlap.unknown -> !schlap.unknown {
; CHECK-NEXT: [[IDLE:%.+]] = schlap.atom "idle" : !schlap.unknown
; CHECK-NEXT: schlap.yield [[IDLE]] : !schlap.unknown
; CHECK: schlap.tuple %{{.+}}, %{{.+}} : (!schlap.unknown, !schlap.unknown) -> !schlap.unknown
; CHECK: schlap.next {
; CHECK: schlap.apply [[F]], %{{.+}} : !schlap.unknown
; CHECK: schlap.update [[F]], %{{.+}}, %{{.+}} : !schlap.unknown
; CHECK: schlap.tuple_get [[T]][1] : !schlap.unknown
(define-spec Values
  (variables f t)
  (init {f = (↦ ((p ∈ (set-of 0 1))) 'idle) ∧ t = (tuple 0 ⊤)})
  (next {(′ f) = (update f 0 (@ f 1)) ∧ (′ t) = (tuple 1 (tuple-ref t 1))}))

(write Values)
(newline)
