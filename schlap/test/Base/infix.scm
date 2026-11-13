; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap core)
        (schlap infix))

;; Create a func.func Name in the schlap module that returns its body.
(define (%create-test Loc Name NumArgs BodyFn)
  (define ArgTypes (make-list NumArgs !schlap.unknown))
  (define FuncOp
    (create-op "func.func"
               (loc: Loc)
               (operands:)
               (attributes:
                 ("sym_name" (string-attr Name))
                 ("function_type"
                   (type-attr (%function-type (list->vector ArgTypes)
                                              (vector !schlap.unknown)))))
               (result-types:)
               (region: "body" () 0)))
  (with-builder (lambda ()
    (define Block (entry-block FuncOp))
    (define Args
      (map (lambda (T) (add-argument Block T Loc)) ArgTypes))
    (at-block-begin Block)
    (let ((Result (lift* Loc (apply BodyFn Args))))
      (create-op "func.return"
                 (loc: Loc)
                 (operands: Result)
                 (attributes:)
                 (result-types:)))
    (if #f #f))))

(define-syntax define-test
  (syntax-rules ()
    ((_ Name (Arg ...) Body ...)
     (%create-test (syntax-source-loc Name) 'Name (length '(Arg ...))
                   (lambda (Arg ...) Body ...)))))

;; Test each operator alone.

; CHECK-LABEL: func.func @and_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.and [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]] : !schlap.unknown
(define-test and_op (a b) {a ∧ b})

; CHECK-LABEL: func.func @or_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.or [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test or_op (a b) {a ∨ b})

; CHECK-LABEL: func.func @not_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.not [[A]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test not_op (a) (¬ a))

; CHECK-LABEL: func.func @implies_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test implies_op (a b) {a ⇒ b})

; CHECK-LABEL: func.func @iff_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.iff [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test iff_op (a b) {a ⇔ b})

; CHECK-LABEL: func.func @eq_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.eq [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test eq_op (a b) {a = b})

; CHECK-LABEL: func.func @ne_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.ne [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test ne_op (a b) {a ≠ b})

; CHECK-LABEL: func.func @lt_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.lt [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test lt_op (a b) {a < b})

; CHECK-LABEL: func.func @le_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.le [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test le_op (a b) {a ≤ b})

; CHECK-LABEL: func.func @gt_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.gt [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test gt_op (a b) {a > b})

; CHECK-LABEL: func.func @ge_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.ge [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test ge_op (a b) {a ≥ b})

; CHECK-LABEL: func.func @add_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.add [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test add_op (a b) {a + b})

; CHECK-LABEL: func.func @sub_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.sub [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test sub_op (a b) {a - b})

; CHECK-LABEL: func.func @neg_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.neg [[A]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test neg_op (a) {- a})

; CHECK-LABEL: func.func @mul_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.mul [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test mul_op (a b) {a * b})

; CHECK-LABEL: func.func @member_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[S:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.member [[A]], [[S]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test member_op (a S) {a ∈ S})

; CHECK-LABEL: func.func @not_member_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[S:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.not_member [[A]], [[S]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test not_member_op (a S) {a ∉ S})

; CHECK-LABEL: func.func @union_op(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown, [[T:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.union [[S]], [[T]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test union_op (S T) {S ∪ T})

; CHECK-LABEL: func.func @intersect_op(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown, [[T:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.intersect [[S]], [[T]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test intersect_op (S T) {S ∩ T})

; CHECK-LABEL: func.func @subset_op(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown, [[T:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.subset [[S]], [[T]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test subset_op (S T) {S ⊆ T})

; CHECK-LABEL: func.func @range_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.range [[A]], [[B]] : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test range_op (a b) {a ‥ b})

; CHECK-LABEL: func.func @empty_set_op(
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.empty_set : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test empty_set_op () ∅)

; CHECK-LABEL: func.func @true_op(
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.true : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test true_op () ⊤)

; CHECK-LABEL: func.func @false_op(
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.false : !schlap.unknown
; CHECK-NEXT: return [[R]]
(define-test false_op () ⊥)

;; Test the prefix forms.

; CHECK-LABEL: func.func @prefix(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R0:%[0-9]+]] = schlap.ge [[A]], [[B]]
; CHECK-NEXT: [[R1:%[0-9]+]] = schlap.range [[A]], [[B]]
; CHECK-NEXT: [[R2:%[0-9]+]] = schlap.member [[R0]], [[R1]]
; CHECK-NEXT: [[R3:%[0-9]+]] = schlap.not [[R2]]
; CHECK-NEXT: return [[R3]]
(define-test prefix (a b) (¬ (∈ (≥ a b) (‥ a b))))

;; Test the precedence between adjacent levels.

; CHECK-LABEL: func.func @prec_iff_implies(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.implies [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.implies [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.iff [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_iff_implies (a b c d) {a ⇒ b ⇔ c ⇒ d})

; CHECK-LABEL: func.func @prec_implies_and(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.and [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.or [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_implies_and (a b c d) {a ∧ b ⇒ c ∨ d})

; CHECK-LABEL: func.func @prec_and_cmp(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.eq [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.lt [[C]], [[D]]
; CHECK-NEXT: [[ABCD:%[0-9]+]] = schlap.and [[AB]], [[CD]]
; CHECK-NEXT: [[AD:%[0-9]+]] = schlap.member [[A]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.and [[ABCD]], [[AD]]
; CHECK-NEXT: return [[R]]
(define-test prec_and_cmp (a b c d) {a = b ∧ c < d ∧ a ∈ d})

; CHECK-LABEL: func.func @prec_or_cmp(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.ne [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.not_member [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.or [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_or_cmp (a b c d) {a ≠ b ∨ c ∉ d})

; CHECK-LABEL: func.func @prec_cmp_set(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.union [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.union [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.subset [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_cmp_set (a b c d) {a ∪ b ⊆ c ∪ d})

; CHECK-LABEL: func.func @prec_set_range(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.range [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.range [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.intersect [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_set_range (a b c d) {a ‥ b ∩ c ‥ d})

; CHECK-LABEL: func.func @prec_range_add(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.add [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.sub [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.range [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_range_add (a b c d) {a + b ‥ c - d})

; CHECK-LABEL: func.func @prec_add_mul(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.mul [[A]], [[B]]
; CHECK-NEXT: [[CD:%[0-9]+]] = schlap.mul [[C]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.sub [[AB]], [[CD]]
; CHECK-NEXT: return [[R]]
(define-test prec_add_mul (a b c d) {a * b - c * d})

; Test every level in one expression.
; CHECK-LABEL: func.func @prec_all(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown, [[D:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[MUL:%[0-9]+]] = schlap.mul [[B]], [[C]]
; CHECK-NEXT: [[ADD:%[0-9]+]] = schlap.add [[A]], [[MUL]]
; CHECK-NEXT: [[RANGE:%[0-9]+]] = schlap.range [[ADD]], [[D]]
; CHECK-NEXT: [[UNION:%[0-9]+]] = schlap.union [[RANGE]], [[B]]
; CHECK-NEXT: [[MEMBER:%[0-9]+]] = schlap.member [[A]], [[UNION]]
; CHECK-NEXT: [[AND:%[0-9]+]] = schlap.and [[MEMBER]], [[C]]
; CHECK-NEXT: [[IMPLIES:%[0-9]+]] = schlap.implies [[AND]], [[D]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.iff [[IMPLIES]], [[A]]
; CHECK-NEXT: return [[R]]
(define-test prec_all (a b c d) {a ∈ a + b * c ‥ d ∪ b ∧ c ⇒ d ⇔ a})

; CHECK-LABEL: func.func @nested(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.or [[A]], [[B]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.and [[AB]], [[C]]
; CHECK-NEXT: return [[R]]
(define-test nested (a b c) {{a ∨ b} ∧ c})

;; Test associativity.

; CHECK-LABEL: func.func @left_and(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.and [[A]], [[B]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.and [[AB]], [[C]]
; CHECK-NEXT: return [[R]]
(define-test left_and (a b c) {a ∧ b ∧ c})

; CHECK-LABEL: func.func @left_or_mixed(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.or [[A]], [[B]]
; CHECK-NEXT: [[ABC:%[0-9]+]] = schlap.or [[AB]], [[C]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[ABC]], [[A]]
; CHECK-NEXT: return [[R]]
(define-test left_or_mixed (a b c) {a ∨ b ∨ c ⇒ a})

; CHECK-LABEL: func.func @left_union(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.union [[A]], [[B]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.union [[AB]], [[C]]
; CHECK-NEXT: return [[R]]
(define-test left_union (a b c) {a ∪ b ∪ c})

; CHECK-LABEL: func.func @left_intersect_mixed(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.intersect [[A]], [[B]]
; CHECK-NEXT: [[ABC:%[0-9]+]] = schlap.intersect [[AB]], [[C]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.subset [[ABC]], [[A]]
; CHECK-NEXT: return [[R]]
(define-test left_intersect_mixed (a b c) {a ∩ b ∩ c ⊆ a})

; CHECK-LABEL: func.func @left_add_sub(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.sub [[A]], [[B]]
; CHECK-NEXT: [[ABC:%[0-9]+]] = schlap.add [[AB]], [[C]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.sub [[ABC]], [[A]]
; CHECK-NEXT: return [[R]]
(define-test left_add_sub (a b c) {a - b + c - a})

; CHECK-LABEL: func.func @left_sub(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.sub [[A]], [[B]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.sub [[AB]], [[C]]
; CHECK-NEXT: return [[R]]
(define-test left_sub (a b c) {a - b - c})

; CHECK-LABEL: func.func @left_mul(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.mul [[A]], [[B]]
; CHECK-NEXT: [[ABC:%[0-9]+]] = schlap.mul [[AB]], [[C]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.add [[ABC]], [[A]]
; CHECK-NEXT: return [[R]]
(define-test left_mul (a b c) {a * b * c + a})

; CHECK-LABEL: func.func @right_implies(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[BC:%[0-9]+]] = schlap.implies [[B]], [[C]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[A]], [[BC]]
; CHECK-NEXT: return [[R]]
(define-test right_implies (a b c) {a ⇒ b ⇒ c})

; CHECK-LABEL: func.func @right_implies_mixed(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.and [[A]], [[B]]
; CHECK-NEXT: [[CA:%[0-9]+]] = schlap.or [[C]], [[A]]
; CHECK-NEXT: [[BCA:%[0-9]+]] = schlap.implies [[B]], [[CA]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[AB]], [[BCA]]
; CHECK-NEXT: return [[R]]
(define-test right_implies_mixed (a b c) {a ∧ b ⇒ b ⇒ c ∨ a})

;; Test unary minus.

; CHECK-LABEL: func.func @neg_term(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown, [[B:%arg[0-9]+]]: !schlap.unknown,
; CHECK-SAME: [[C:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[AB:%[0-9]+]] = schlap.mul [[A]], [[B]]
; CHECK-NEXT: [[NAB:%[0-9]+]] = schlap.neg [[AB]]
; CHECK-NEXT: [[NC:%[0-9]+]] = schlap.neg [[C]]
; CHECK-NEXT: [[NNC:%[0-9]+]] = schlap.neg [[NC]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.sub [[NAB]], [[NNC]]
; CHECK-NEXT: return [[R]]
(define-test neg_term (a b c) {- a * b - - - c})

;; Test lifting integers, booleans and ∅.

; CHECK-LABEL: func.func @lift_int(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[ONE:%[0-9]+]] = schlap.int 1 : !schlap.unknown
; CHECK-NEXT: [[ADD:%[0-9]+]] = schlap.add [[A]], [[ONE]]
; CHECK-NEXT: [[MIN:%[0-9]+]] = schlap.int -9 : !schlap.unknown
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.lt [[MIN]], [[ADD]]
; CHECK-NEXT: return [[R]]
(define-test lift_int (a) {-9 < a + 1})

; CHECK-LABEL: func.func @lift_bool(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[T:%[0-9]+]] = schlap.true : !schlap.unknown
; CHECK-NEXT: [[AT:%[0-9]+]] = schlap.and [[A]], [[T]]
; CHECK-NEXT: [[F:%[0-9]+]] = schlap.false : !schlap.unknown
; CHECK-NEXT: [[T2:%[0-9]+]] = schlap.true : !schlap.unknown
; CHECK-NEXT: [[FT:%[0-9]+]] = schlap.or [[F]], [[T2]]
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.implies [[AT]], [[FT]]
; CHECK-NEXT: return [[R]]
(define-test lift_bool (a) {a ∧ ⊤ ⇒ ⊥ ∨ #t})

; CHECK-LABEL: func.func @lift_empty_set(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[E:%[0-9]+]] = schlap.empty_set : !schlap.unknown
; CHECK-NEXT: [[SE:%[0-9]+]] = schlap.union [[S]], [[E]]
; CHECK-NEXT: [[E2:%[0-9]+]] = schlap.empty_set : !schlap.unknown
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.eq [[SE]], [[E2]]
; CHECK-NEXT: return [[R]]
(define-test lift_empty_set (S)
  (define e ∅)
  {(∪ S ∅) = e})

;; Test the binders.

; CHECK-LABEL: func.func @forall_set(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.forall ([[X:%arg[0-9]+]]: !schlap.unknown) in [[S]] : !schlap.unknown -> !schlap.unknown {
; CHECK-NEXT: [[ZERO:%[0-9]+]] = schlap.int 0 : !schlap.unknown
; CHECK-NEXT: [[LE:%[0-9]+]] = schlap.le [[ZERO]], [[X]]
; CHECK-NEXT: schlap.yield [[LE]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: return [[R]]
(define-test forall_set (S) (∀ ((x ∈ S)) {0 ≤ x}))

; CHECK-LABEL: func.func @exists_type(
; CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.exists ([[X:%arg[0-9]+]]: !schlap.unknown) in [[A]] : !schlap.unknown -> !schlap.unknown {
; CHECK-NEXT: [[EQ:%[0-9]+]] = schlap.eq [[X]], [[X]]
; CHECK-NEXT: schlap.yield [[EQ]] : !schlap.unknown
; CHECK-NEXT: }
; CHECK-NEXT: return [[R]]
(define-test exists_type (A) (∃ ((x : A)) {x = x}))

; Nest one binder per variable and evaluate a Scheme body.
; CHECK-LABEL: func.func @forall_two(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown, [[A:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.forall ([[X:%arg[0-9]+]]: !schlap.unknown) in [[S]]
; CHECK-NEXT: [[INNER:%[0-9]+]] = schlap.forall ([[Y:%arg[0-9]+]]: !schlap.unknown) in [[A]]
; CHECK-NEXT: [[SUM:%[0-9]+]] = schlap.add [[X]], [[Y]]
; CHECK-NEXT: [[EQ:%[0-9]+]] = schlap.eq [[SUM]], [[Y]]
; CHECK-NEXT: schlap.yield [[EQ]]
; CHECK-NEXT: }
; CHECK-NEXT: schlap.yield [[INNER]]
; CHECK-NEXT: }
; CHECK-NEXT: return [[R]]
(define-test forall_two (S A)
  (∀ ((x ∈ S) (y : A))
    (define z {x + y})
    {z = y}))

; CHECK-LABEL: func.func @exists_two(
; CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.unknown)
; CHECK-NEXT: [[E:%[0-9]+]] = schlap.empty_set
; CHECK-NEXT: [[R:%[0-9]+]] = schlap.exists ([[X:%arg[0-9]+]]: !schlap.unknown) in [[E]]
; CHECK-NEXT: [[INNER:%[0-9]+]] = schlap.exists ([[Y:%arg[0-9]+]]: !schlap.unknown) in [[S]]
; CHECK-NEXT: [[T:%[0-9]+]] = schlap.true
; CHECK-NEXT: schlap.yield [[T]]
; CHECK-NEXT: }
; CHECK-NEXT: schlap.yield [[INNER]]
; CHECK-NEXT: }
; CHECK-NEXT: return [[R]]
(define-test exists_two (S) (∃ ((x ∈ ∅) (y ∈ S)) ⊤))

(write schlap-current-module)
(newline)

(unless (verify schlap-current-module)
  (error "module failed verification"))
