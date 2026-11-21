; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>/dev/null | FileCheck %s
; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s \
; RUN:   2>&1 >/dev/null | FileCheck %s --check-prefix=LOC
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap base)
        (schlap infix)
        (schlap rule))

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

; Create a rule with variables, init, events and requirements.
; CHECK-LABEL: schlap.rule @Invalidate {
; CHECK-NEXT: [[INVALID:%.+]] = schlap.variable "invalid" : !schlap.unknown
; CHECK-NEXT: schlap.init {
; CHECK: schlap.yield
; CHECK-NEXT: }
; CHECK-NEXT: schlap.on_mutate {
; CHECK-NEXT: ^bb0([[V:%.+]]: !schlap.unknown):
; CHECK-NEXT: [[PRIME:%.+]] = schlap.prime [[INVALID]]
; CHECK-NEXT: [[ROOT:%.+]] = schlap.root [[V]]
; CHECK-NEXT: [[READ:%.+]] = schlap.read_from [[ROOT]]
; CHECK-NEXT: [[UNION:%.+]] = schlap.union [[INVALID]], [[READ]]
; CHECK-NEXT: [[EQ:%.+]] = schlap.eq [[PRIME]], [[UNION]]
; CHECK-NEXT: schlap.yield [[EQ]]
; CHECK-NEXT: }
; CHECK-NEXT: schlap.on_use {
; CHECK-NEXT: ^bb0([[V:%.+]]: !schlap.unknown):
; CHECK-NEXT: [[NOT:%.+]] = schlap.not_member [[V]], [[INVALID]]
; CHECK-NEXT: schlap.require [[NOT]]
; CHECK-NEXT: schlap.done
; CHECK-NEXT: }
; CHECK-NEXT: }
(define-rule Invalidate
  (variables invalid)
  (init {invalid = ∅})
  (on-mutate (v)
    {(′ invalid) = {invalid ∪ (read-from (root v))}})
  (on-use (v)
    (require {v ∉ invalid})))
(write Invalidate)
(newline)

; Take the values that a mutated value may own.
; CHECK-LABEL: schlap.rule @InvalidateOwned {
; CHECK: schlap.on_mutate {
; CHECK-NEXT: ^bb0([[V:%.+]]: !schlap.unknown):
; CHECK-NEXT: [[PRIME:%.+]] = schlap.prime
; CHECK-NEXT: [[OWNED:%.+]] = schlap.owned_by [[V]]
; CHECK-NEXT: schlap.union {{%.+}}, [[OWNED]]
(define-rule InvalidateOwned
  (variables invalid)
  (init {invalid = ∅})
  (on-mutate (v)
    {(′ invalid) = {invalid ∪ (owned-by v)}}))
(write InvalidateOwned)
(newline)

; Require each value to be used at most once.
(define-rule UseOnce
  (variables used)
  (init {used = ∅})
  (on-use (v)
    (require {v ∉ used})
    {(′ used) = {used ∪ (set-of v)}}))

;; Create functions of two i32 arguments in a module of their own.
(load-dialect "func")
(load-dialect "arith")
(define i32 (type "i32"))
(define Other (create-top-module "other"))

(define (make-fn Loc Name BodyFn)
  (with-module-builder Other
    (lambda ()
      (create-op* "func.func" Loc
                  (list (list "sym_name" (string-attr Name))
                        (list "function_type"
                              (type-attr (%function-type (vector i32 i32) #()))))
                  '() '() (list (list (list i32 i32) BodyFn))))))

(define-syntax define-function
  (syntax-rules ()
    ((_ (Name X Y) Body ...)
     (define Name
       (make-fn (syntax-source-loc Name) 'Name
                (lambda (X Y)
                  Body ...
                  (create-op* "func.return" (syntax-source-loc Name)
                              '() '() '() '())))))))

(define-syntax addi
  (syntax-rules ()
    ((_ A B)
     (result (create-op* "arith.addi" (syntax-source-loc) '() (list A B)
                         (list i32) '())))))

(define-function (pair X Y)
  (addi X Y))

; Locate the use, the function and each step in the function.
; LOC: rule.scm:[[@LINE+6]]:3
; LOC: rule.scm:[[@LINE+4]]:18
; LOC: rule.scm:[[@LINE+4]]:9
; LOC: rule.scm:[[@LINE+3]]:9
; LOC: rule.scm:[[@LINE+2]]:3
(define-function (chain X Y)
  (addi (addi X Y) X))

; Count the states of rules that hold.
; CHECK: 3
(write (check-rule Invalidate pair))
(newline)
; CHECK: 4
(write (check-rule UseOnce pair))
(newline)

; Report a violation with the trace through the function.
; CHECK: "rule UseOnce does not hold for function: chain"
; CHECK-NEXT: "invariant use of v0 by arith.addi #1 is violated after 3 steps"
; CHECK-NEXT: "initial state: used = {}, pc = 0"
; CHECK-NEXT: "step 1, arith.addi #0 uses v0: used = {v0}, pc = 1"
; CHECK-NEXT: "step 2, arith.addi #0 uses v1 defines v2: used = {v0, v1}, pc = 2"
; CHECK-NEXT: "step 3, arith.addi #1 uses v2: used = {v0, v1, v2}, pc = 3"
(write-error (lambda () (check-rule UseOnce chain)))

; Report the state limit.
; CHECK: "rule UseOnce does not hold for function: pair"
; CHECK-NEXT: "spec UseOnce(pair) exceeds the state limit of 1 states"
(write-error (lambda () (check-rule UseOnce pair 'state-limit 1)))

; Reject an operation that is not a function.
; CHECK: "expecting a function with a body: {{.*}}"
(write-error (lambda () (check-rule UseOnce Invalidate)))
