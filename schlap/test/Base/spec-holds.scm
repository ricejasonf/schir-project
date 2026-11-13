; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

; Check the jugs without asking for four gallons.
(define-spec DieHard
  (variables small big)

  (define (Min m n) (ite {m < n} m n))

  (init {small = 0 ∧ big = 0})

  (action FillSmall  {(′ small) = 3 ∧ (unchanged big)})
  (action FillBig    {(′ big) = 5 ∧ (unchanged small)})
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
  (invariant NotOverfull {small + big ≤ 8}))

; CHECK: 16
(write (check DieHard))
(newline)

; Count with a boolean flag and an if-then-else action.
(define-spec Toggle
  (variables n on)
  (init {n = 0 ∧ on = ⊥})
  (next (ite on
             {(′ on) = ⊥ ∧ (′ n) = {n + 1}}
             {n < 3 ∧ (′ on) = ⊤ ∧ (unchanged n)}))
  (invariant Bounded {n ≤ 3 ∧ {on ⇒ n < 3} ∧ {on ⇔ (¬ (¬ on))}}))

; CHECK-NEXT: 7
(write (check Toggle 'deadlock #f))
(newline)

; Check the same spec again.
; CHECK-NEXT: 7
(write (check Toggle 'deadlock #f))
(newline)
