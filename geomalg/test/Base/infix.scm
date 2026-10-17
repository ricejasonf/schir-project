; RUN: schir-scheme -I %schir_module_path -I %geomalg_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) + - * /)
        (schir mlir)
        (geomalg base)
        (geomalg infix))

; CHECK-LABEL: func.func @sum_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[SUM:%[0-9]+]] = "geomalg.sum"([[A]], [[B]], [[C]])
; CHECK-NEXT: geomalg.return [[SUM]]
(define-func sum_op ((a : !e1) (b : !e2) (c : !e3))
  {a + b + c})

; CHECK-LABEL: func.func @sub_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[NB:%[0-9]+]] = "geomalg.negate"([[B]])
; CHECK-NEXT: [[NC:%[0-9]+]] = "geomalg.negate"([[C]])
; CHECK-NEXT: [[SUM:%[0-9]+]] = "geomalg.sum"([[A]], [[NB]], [[NC]])
; CHECK-NEXT: geomalg.return [[SUM]]
(define-func sub_op ((a : !e1) (b : !e2) (c : !e3))
  {a - b - c})

; CHECK-LABEL: func.func @neg_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>)
; CHECK-NEXT: [[NA:%[0-9]+]] = "geomalg.negate"([[A]])
; CHECK-NEXT: geomalg.return [[NA]]
(define-func neg_op ((a : !e1))
  {- a})

; CHECK-LABEL: func.func @gprod_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.gprod"([[A]], [[B]])
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.gprod"([[AB]], [[C]])
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func gprod_op ((a : !e1) (b : !e2) (c : !e3))
  {a * b * c})

; CHECK-LABEL: func.func @div_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[IB:%[0-9]+]] = "geomalg.inverse"([[B]])
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.gprod"([[A]], [[IB]])
; CHECK-NEXT: [[IC:%[0-9]+]] = "geomalg.inverse"([[C]])
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.gprod"([[AB]], [[IC]])
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func div_op ((a : !e1) (b : !e2) (c : !e3))
  {a / b / c})

; The left contraction is right associative.
; CHECK-LABEL: func.func @lc_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[BC:%[0-9]+]] = "geomalg.iprod"([[B]], [[C]])
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.iprod"([[A]], [[BC]])
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func lc_op ((a : !e1) (b : !e2) (c : !e3))
  {a ⌋ b ⌋ c})

; CHECK-LABEL: func.func @wedge_op(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.oprod"([[A]], [[B]])
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.oprod"([[AB]], [[C]])
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func wedge_op ((a : !e1) (b : !e2) (c : !e3))
  {a ∧ b ∧ c})

; CHECK-LABEL: func.func @sandwich(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.blade<3>)
; CHECK-NEXT: [[P0:%[0-9]+]] = "geomalg.gprod"([[A]], [[B]])
; CHECK-NEXT: [[P1:%[0-9]+]] = "geomalg.gprod"([[P0]], [[X]])
; CHECK-NEXT: [[IB:%[0-9]+]] = "geomalg.inverse"([[B]])
; CHECK-NEXT: [[P2:%[0-9]+]] = "geomalg.gprod"([[P1]], [[IB]])
; CHECK-NEXT: [[IA:%[0-9]+]] = "geomalg.inverse"([[A]])
; CHECK-NEXT: [[P3:%[0-9]+]] = "geomalg.gprod"([[P2]], [[IA]])
; CHECK-NEXT: geomalg.return [[P3]]
(define-func sandwich ((a : !e1) (b : !e2) (A : (!blade !e1 !e2)))
  {a * b * A / b / a})

; The outer product binds tighter than the left contraction
; which binds tighter than the geometric product.
; CHECK-LABEL: func.func @precedence(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.oprod"([[A]], [[B]])
; CHECK-NEXT: [[LC:%[0-9]+]] = "geomalg.iprod"([[AB]], [[C]])
; CHECK-NEXT: [[GP:%[0-9]+]] = "geomalg.gprod"([[LC]], [[A]])
; CHECK-NEXT: [[BC:%[0-9]+]] = "geomalg.oprod"([[B]], [[C]])
; CHECK-NEXT: [[SUM:%[0-9]+]] = "geomalg.sum"([[GP]], [[BC]])
; CHECK-NEXT: geomalg.return [[SUM]]
(define-func precedence ((a : !e1) (b : !e2) (c : !e3))
  {a ∧ b ⌋ c * a + b ∧ c})

; CHECK-LABEL: func.func @mixed_sum(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.gprod"([[A]], [[B]])
; CHECK-NEXT: [[NAB:%[0-9]+]] = "geomalg.negate"([[AB]])
; CHECK-NEXT: [[IA:%[0-9]+]] = "geomalg.inverse"([[A]])
; CHECK-NEXT: [[CA:%[0-9]+]] = "geomalg.gprod"([[C]], [[IA]])
; CHECK-NEXT: [[NCA:%[0-9]+]] = "geomalg.negate"([[CA]])
; CHECK-NEXT: [[SUM:%[0-9]+]] = "geomalg.sum"([[NAB]], [[B]], [[NCA]])
; CHECK-NEXT: geomalg.return [[SUM]]
(define-func mixed_sum ((a : !e1) (b : !e2) (c : !e3))
  {- a * b + b - c / a})

; CHECK-LABEL: func.func @nested(
; CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.blade<1>,
; CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<2>,
; CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<4>)
; CHECK-NEXT: [[AB:%[0-9]+]] = "geomalg.sum"([[A]], [[B]])
; CHECK-NEXT: [[ABC:%[0-9]+]] = "geomalg.oprod"([[AB]], [[C]])
; CHECK-NEXT: geomalg.return [[ABC]]
(define-func nested ((a : !e1) (b : !e2) (c : !e3))
  {{a + b} ∧ c})

(write geomalg-current-module)
(newline)

(unless (verify geomalg-current-module)
  (error "module failed verification"))
