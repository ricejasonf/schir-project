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

; Report a state with no next state at the spec with its trace.
; CHECK: "check failed for spec: Countdown"
; CHECK-NEXT: "spec Countdown deadlocks after 2 steps"
; CHECK-NEXT: "initial state: n = 2"
; CHECK-NEXT: "step 1, Dec: n = 1"
; CHECK-NEXT: "step 2, Dec: n = 0"
; LOC: spec-check-errors.scm:[[@LINE+1]]:
(define-spec Countdown
  (variables n)
  (init {n = 2})
  (action Dec {n > 0 ∧ (′ n) = {n - 1}})
  (next Dec))
(write-error (lambda () (check Countdown)))

; Accept the deadlock when deadlocks are not checked.
; CHECK: 3
(write (check Countdown 'deadlock #f))
(newline)

; Report the state limit at the spec.
; CHECK: "check failed for spec: Counter"
; CHECK-NEXT: "spec Counter exceeds the state limit of 10 states"
; LOC: spec-check-errors.scm:[[@LINE+1]]:
(define-spec Counter
  (variables n)
  (init {n = 0})
  (next {(′ n) = {n + 1}}))
(write-error (lambda () (check Counter 'state-limit 10)))

; Report integer overflow at the operation with the trace.
; CHECK: "check failed for spec: Overflow"
; CHECK-NEXT: "integer overflow"
; CHECK-NEXT: "initial state: n = 1"
; CHECK: "step 6, Grow: n = 1000000000000000000"
(define-spec Overflow
  (variables n)
  (init {n = 1})
  ; LOC: spec-check-errors.scm:[[@LINE+1]]:
  (action Grow {(′ n) = {n * 1000}})
  (next Grow))
(write-error (lambda () (check Overflow)))

; Report a function applied outside its domain.
; CHECK: "check failed for spec: Domain"
; CHECK-NEXT: "the argument is not in the domain of the function"
(define-spec Domain
  (variables f i)
  (init {f = (↦ ((p ∈ {0 ‥ 1})) p) ∧ i = 0})
  ; LOC: spec-check-errors.scm:[[@LINE+1]]:
  (next {(′ i) = {i + 1} ∧ (′ f) = (update f i (@ f i))}))
(write-error (lambda () (check Domain)))

; Report an invariant that fails in an initial state.
; CHECK: "check failed for spec: BadStart"
; CHECK-NEXT: "invariant Positive is violated after 0 steps"
; CHECK-NEXT: "initial state: n = 0"
(define-spec BadStart
  (variables n)
  (init {n ∈ {0 ‥ 2}})
  (next (unchanged n))
  ; LOC: spec-check-errors.scm:[[@LINE+1]]:
  (invariant Positive {n > 0}))
(write-error (lambda () (check BadStart)))

; Reject an unknown option.
; CHECK: "unknown check option: limit"
(write-error (lambda () (check BadStart 'limit 3)))
