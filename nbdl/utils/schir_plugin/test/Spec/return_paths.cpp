// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace {
namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op result))

(export-c max_i32 sum_to_i32 sum_sums_i32)
(export-c-internal is_zero_i32 add_i32 sub_one_i32)
(export-shader vs_main max_i32 sum_to_i32 sum_sums_i32
               is_zero_i32 add_i32 sub_one_i32)
(export-spirv-binary shaders)

(define i1 (type "i1"))
(define i32 (type "i32"))

(define (constant-i32 Value)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr Value i32)))
                     (result-types: i32))))

(define (cmpi Predicate A B)
  (result (create-op "arith.cmpi"
                     (loc: 0)
                     (operands: A B)
                     (attributes: ("predicate" (attr Predicate (type "i64"))))
                     (result-types: i1))))

(define-fn is_zero_i32 ((X : i32)) -> (i1)
  (return (cmpi "0" X (constant-i32 "0"))))

(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (result (create-op "arith.addi"
                             (loc: 0)
                             (operands: A B)
                             (attributes:)
                             (result-types: i32)))))

(define-fn sub_one_i32 ((X : i32)) -> (i32)
  (return (result (create-op "arith.subi"
                             (loc: 0)
                             (operands: X (constant-i32 "1"))
                             (attributes:)
                             (result-types: i32)))))

; // Yield the result of each branch from the scf.if and return it.
; // CHECK-LABEL: func.func @"::foo::max_i32"
; // CHECK-SAME: (%arg0: i32, %arg1: i32) -> i32 {
; // CHECK-NEXT: [[GT:%.+]] = arith.cmpi sgt, %arg0, %arg1 : i32
; // CHECK-NEXT: [[MAX:%.+]] = scf.if [[GT]] -> (i32) {
; // CHECK-NEXT: scf.yield %arg0 : i32
; // CHECK-NEXT: } else {
; // CHECK-NEXT: scf.yield %arg1 : i32
; // CHECK-NEXT: }
; // CHECK-NEXT: return [[MAX]] : i32
(define-fn max_i32 ((A : i32) (B : i32)) -> (i32)
  (match-if (cmpi "4" A B)
    (return A)
    (return B)))

; // Sum the integers from N down to 1 adding to Acc.
(define-match-fn sum_down (N Acc Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn Acc)
    (visit sum_down (visit sub_one_i32 N)
                    (visit add_i32 Acc N)
                    Fn)))

; // Exit the loop with the result of the return and
; // pass poison in its place when continuing.
; // CHECK-LABEL: func.func @"::foo::sum_to_i32"
; // CHECK-SAME: (%arg0: i32) -> i32 {
; // CHECK: [[WHILE:%.+]]:3 = scf.while ([[N:%.+]] = %arg0, [[ACC:%.+]] = %c0_i32)
; // CHECK-SAME: : (i32, i32) -> (i32, i32, i32) {
; // CHECK: [[IF:%.+]]:4 = scf.if {{%.+}} -> (i1, i32, i32, i32) {
; // CHECK-NEXT: %false = arith.constant false
; // CHECK-NEXT: scf.yield %false, [[N]], [[ACC]], [[ACC]] : i1, i32, i32, i32
; // CHECK-NEXT: } else {
; // CHECK: %true = arith.constant true
; // CHECK-NEXT: [[POISON:%.+]] = ub.poison : i32
; // CHECK-NEXT: scf.yield %true, {{%.+}}, {{%.+}}, [[POISON]] : i1, i32, i32, i32
; // CHECK-NEXT: }
; // CHECK-NEXT: scf.condition([[IF]]#0) [[IF]]#1, [[IF]]#2, [[IF]]#3 : i32, i32, i32
; // CHECK-NEXT: } do {
; // CHECK-NEXT: ^bb0(%arg1: i32, %arg2: i32, %arg3: i32):
; // CHECK-NEXT: scf.yield %arg1, %arg2 : i32, i32
; // CHECK-NEXT: }
; // CHECK-NEXT: return [[WHILE]]#2 : i32
(define-fn sum_to_i32 ((N : i32)) -> (i32)
  (visit sum_down N 0 (match-fn (Sum) (return Sum))))

; // Sum the sums from N down to 1 adding to Acc via nested loops.
(define-match-fn sum_sums_down (N Acc Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn Acc)
    (visit sum_down N Acc
      (match-fn (Next)
        (visit sum_sums_down (visit sub_one_i32 N) Next Fn)))))

; // Pass the result through the iteration values of the outer loop.
; // CHECK-LABEL: func.func @"::foo::sum_sums_i32"
; // CHECK-SAME: (%arg0: i32) -> i32 {
; // CHECK: [[OUTER:%.+]]:3 = scf.while
; // CHECK-SAME: : (i32, i32) -> (i32, i32, i32) {
; // CHECK: scf.while
; // CHECK-SAME: : (i32, i32) -> (i32, i32, i1, i32, i32, i32) {
; // CHECK: return [[OUTER]]#2 : i32
(define-fn sum_sums_i32 ((N : i32)) -> (i32)
  (visit sum_sums_down N 0 (match-fn (Sum) (return Sum))))

; // Store the result of each branch at the single return.
(define-shader-entrypoint vs_main "Vertex" ((N : i32)) -> (i32)
  (match-if (visit is_zero_i32 N)
    (return (visit sum_to_i32 N))
    (return (visit sum_sums_i32 N))))

(write-lower-module)
(write-spirv-binary "shaders.spv")
(finalize-module)
}

}  // namespace foo
}  // namespace

int main() {
  SCHIR_ASSERT(foo::max_i32(2, 3) == 3);
  SCHIR_ASSERT(foo::max_i32(5, -3) == 5);
  SCHIR_ASSERT(foo::sum_to_i32(0) == 0);
  SCHIR_ASSERT(foo::sum_to_i32(4) == 10);
  // 6 + 3 + 1
  SCHIR_ASSERT(foo::sum_sums_i32(3) == 10);
  SCHIR_ASSERT(foo::sum_sums_i32(0) == 0);
}
