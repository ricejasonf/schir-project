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

; CHECK: "check failed for spec: NotASet"
; CHECK-NEXT: "the right operand of ∈ must be '!schlap.set<!schlap.int>', not '!schlap.int'"
(define-spec NotASet
  (variables small)
  (init {small = 0})
  (next {(′ small) = small})
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (invariant Bad {small ∈ 3}))
(write-error (lambda () (check NotASet)))

; CHECK: "check failed for spec: IntAsFormula"
; CHECK-NEXT: "each operand of ∧ must be a formula, not '!schlap.int'"
(define-spec IntAsFormula
  (variables x)
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (init {x = 0 ∧ x})
  (next {(′ x) = x}))
(write-error (lambda () (check IntAsFormula)))

; CHECK: "check failed for spec: Mismatch"
; CHECK-NEXT: "the operands of = must have one type, not '!schlap.prop' and '!schlap.int'"
(define-spec Mismatch
  (variables x)
  (init {x = 0})
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (next {(′ x) = x ∧ {x < 1} = x}))
(write-error (lambda () (check Mismatch)))

; CHECK: "check failed for spec: SetOfMixed"
; CHECK-NEXT: "the elements of set-of must have one type, not '!schlap.int' and '!schlap.atom'"
(define-spec SetOfMixed
  (variables x)
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (init {x ∈ (set-of 1 'two)})
  (next {(′ x) = x}))
(write-error (lambda () (check SetOfMixed)))

; CHECK: "check failed for spec: ApplyInt"
; CHECK-NEXT: "the function of @ must be a function, not '!schlap.int'"
(define-spec ApplyInt
  (variables x)
  (init {x = 0})
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (next {(′ x) = (@ x 1)}))
(write-error (lambda () (check ApplyInt)))

; CHECK: "check failed for spec: Untyped"
; CHECK-NEXT: "unable to infer the type of variable y"
(define-spec Untyped
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (variables x y)
  (init {x = 0})
  (next {(′ x) = x ∧ (′ y) = y}))
(write-error (lambda () (check Untyped)))

; CHECK: "check failed for spec: PrimeOfValue"
; CHECK-NEXT: "′ applies only to a state variable"
(define-spec PrimeOfValue
  (variables x)
  (init {x = 0})
  ; LOC: spec-type-errors.scm:[[@LINE+1]]:
  (next {(′ x) = (′ {x + 1})}))
(write-error (lambda () (check PrimeOfValue)))
