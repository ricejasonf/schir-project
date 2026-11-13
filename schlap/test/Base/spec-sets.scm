; RUN: not schir-scheme -I %schir_module_path -I %schlap_module_path %s 2>&1 \
; RUN:   | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

; Collect the numbers 0 to 3 into a set and count them in a tuple.
(define-spec Collect
  (variables seen pair)
  (define Nums {0 ‥ 3})
  (init {seen = ∅ ∧ pair = (tuple 0 'none)})
  (action Add
    (∃ ((n ∈ Nums))
      {n ∉ seen ∧
       (′ seen) = {seen ∪ (set-of n)} ∧
       (′ pair) = (tuple {(tuple-ref pair 0) + 1} 'some)}))
  (action Reset
    {seen = Nums ∧ (′ seen) = {seen ∩ ∅} ∧
     (′ pair) ∈ (set-of (tuple 0 'none))})
  (next {Add ∨ Reset})
  (invariant Subset {{seen ⊆ Nums ∧ (¬ {Nums ⊆ ∅})} ∨ ⊥})
  (invariant Count (∃ ((k ∈ {0 ‥ 4})) {(tuple-ref pair 0) = k}))
  (invariant AllSmall (∀ ((n ∈ seen) (m ∈ Nums)) {n < 4 ∧ m ≤ 3}))
  (invariant Sizes {{(tuple-ref pair 0) = 0} ⇔ {seen = ∅}}))

; CHECK: 16
(write (check Collect))
(newline)

; Choose the next value from a set.
(define-spec Pick
  (variables x picked)
  (init {x ∈ (set-of 1 2) ∧ picked = (set-of x)})
  (next {(′ x) ∈ {picked ∪ (set-of {x + 1})} ∧
         (′ picked) = {picked ∪ (set-of (′ x))}})
  ; CHECK: spec-sets.scm:[[@LINE+2]]:{{[0-9]+}}: note: invariant Small is violated after 1 step
  ; CHECK: note: initial state: x = 2, picked = {2}
  (invariant Small {picked ⊆ {1 ‥ 2}}))

; CHECK: note: step 1: x = 3, picked = {2, 3}
; CHECK-NOT: note:
(check Pick)
