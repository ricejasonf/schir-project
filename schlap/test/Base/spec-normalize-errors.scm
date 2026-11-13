; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>/dev/null | FileCheck %s
; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>&1 >/dev/null | FileCheck %s --check-prefix=LOC
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

;; Write the message of the error that Thunk raises and of each note,
;; and the location of each note.
(define (write-error Thunk)
  (guard (E (#t (write (error-object-message E))
                (newline)
                (for-each (lambda (I)
                            (when (error-object? I)
                              (write (error-object-message I))
                              (newline)
                              (dump-source-loc I)))
                          (error-object-irritants E))))
    (Thunk)
    (write "no error")
    (newline)))

; CHECK: "check failed for spec: NoNextValue"
; CHECK-NEXT: "big has no next value in FillSmall"
(define-spec NoNextValue
  (variables small big)
  (init {small = 0 ∧ big = 0})
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (action FillSmall {(′ small) = 3})
  (action FillBig {(′ big) = 5 ∧ (unchanged small)})
  (next {FillSmall ∨ FillBig}))
(write-error (lambda () (check NoNextValue)))

; Accept a variable assigned in next outside the action.
; CHECK: 4
(define-spec AssignedOutside
  (variables small big)
  (init {small = 0 ∧ big = 0})
  (action FillSmall {(′ small) = 3})
  (action EmptySmall {(′ small) = 0})
  (next {{FillSmall ∨ EmptySmall} ∧ (′ big) = {1 - big}}))
(write (check AssignedOutside))
(newline)

; CHECK: "check failed for spec: NoNextValueInNext"
; CHECK-NEXT: "y has no next value in next"
(define-spec NoNextValueInNext
  (variables x y)
  (init {x = 0 ∧ y = 0})
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (next {(′ x) = x ∨ {(′ x) = 1 ∧ (′ y) = 1}}))
(write-error (lambda () (check NoNextValueInNext)))

; CHECK: "check failed for spec: NoInitialValue"
; CHECK-NEXT: "y has no initial value"
(define-spec NoInitialValue
  (variables x y)
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (init {x = 0})
  (next {(unchanged x) ∧ (′ y) = 1}))
(write-error (lambda () (check NoInitialValue)))

; CHECK: "check failed for spec: ReadEarly"
; CHECK-NEXT: "y is read before it is assigned"
(define-spec ReadEarly
  (variables x y)
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (init {x = 0 ∧ y = {y + 1}})
  (next (unchanged x y)))
(write-error (lambda () (check ReadEarly)))

; CHECK: "check failed for spec: ReadNextEarly"
; CHECK-NEXT: "′ x is read before it is assigned"
(define-spec ReadNextEarly
  (variables x)
  (init {x = 0})
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (next {(′ x) > 0 ∧ (′ x) = 1}))
(write-error (lambda () (check ReadNextEarly)))

; CHECK: "check failed for spec: PrimeInInvariant"
; CHECK-NEXT: "′ is only valid in an action or next"
(define-spec PrimeInInvariant
  (variables x)
  (init {x = 0})
  (next (unchanged x))
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (invariant Bad {(′ x) = x}))
(write-error (lambda () (check PrimeInInvariant)))

; CHECK: "check failed for spec: ActionInInit"
; CHECK-NEXT: "an action is only valid in an action or next"
(define-spec ActionInInit
  (variables x)
  (action Stay (unchanged x))
  ; LOC: spec-normalize-errors.scm:[[@LINE+1]]:
  (init {x = 0 ∧ Stay})
  (next Stay))
(write-error (lambda () (check ActionInInit)))
