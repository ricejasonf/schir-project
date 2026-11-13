; RUN: not schir-scheme -I %schir_module_path -I %schlap_module_path %s 2>&1 \
; RUN:   | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

; Find how to measure four gallons with a three and a five gallon jug.
(define-spec DieHard
  (variables small big)

  (define (Min m n) (ite {m < n} m n))

  (init {small = 0 ∧ big = 0})

  (action FillSmall  {(′ small) = 3 ∧ (′ big) = big})
  (action FillBig    {(′ big) = 5 ∧ (′ small) = small})
  (action EmptySmall {(′ small) = 0 ∧ (unchanged big)})
  (action EmptyBig   {(′ big) = 0 ∧ (unchanged small)})
  (action SmallToBig
    (let ((n (Min small {5 - big})))
      {(′ small) = {small - n} ∧ (′ big) = {big + n}}))
  (action BigToSmall
    (let ((n (Min big {3 - small})))
      {(′ big) = {big - n} ∧ (′ small) = {small + n}}))

  (next {FillSmall ∨ FillBig ∨ EmptySmall ∨
         EmptyBig ∨ SmallToBig ∨ BigToSmall})

  (invariant TypeOK {small ∈ {0 ‥ 3} ∧ big ∈ {0 ‥ 5}})
  (invariant NotSolved {big ≠ 4}))

; Report the trace in order after the error at the check.
; CHECK: spec-diehard.scm:[[@LINE+10]]:1: error: check failed for spec: DieHard
; CHECK: spec-diehard.scm:[[@LINE-4]]:{{[0-9]+}}: note: invariant NotSolved is violated after 6 steps
; CHECK: spec-diehard.scm:[[@LINE-22]]:{{[0-9]+}}: note: initial state: small = 0, big = 0
; CHECK: spec-diehard.scm:[[@LINE-20]]:{{[0-9]+}}: note: step 1, FillBig: small = 0, big = 5
; CHECK: spec-diehard.scm:[[@LINE-15]]:{{[0-9]+}}: note: step 2, BigToSmall: small = 3, big = 2
; CHECK: spec-diehard.scm:[[@LINE-21]]:{{[0-9]+}}: note: step 3, EmptySmall: small = 0, big = 2
; CHECK: spec-diehard.scm:[[@LINE-17]]:{{[0-9]+}}: note: step 4, BigToSmall: small = 2, big = 0
; CHECK: spec-diehard.scm:[[@LINE-24]]:{{[0-9]+}}: note: step 5, FillBig: small = 2, big = 5
; CHECK: spec-diehard.scm:[[@LINE-19]]:{{[0-9]+}}: note: step 6, BigToSmall: small = 3, big = 4
; CHECK-NOT: note:
(check DieHard)
