// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv
// RUN: %t

#include <nbdl/bind_memref.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <array>
#include <cstdint>
#include <mdspan>
#include <vector>

namespace {
namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op load-dialect result))

(load-dialect "memref")
(export-c max_i32 sum_to_i32 sum_sums_i32 count_down_i32 max_k_i32
          sum_to_k_i32 early_max_i32 clamp_k_i32 sum_if_positive_i32
          find_positive_i32 find_positive_2d_i32 store_while_positive)
(export-c-internal is_zero_i32 is_positive_i32 add_i32 sub_one_i32
                   store_first_i32)
(export-shader vs_main max_i32 sum_to_i32 sum_sums_i32 count_down_i32
               early_max_i32 clamp_k_i32 sum_if_positive_i32
               is_zero_i32 is_positive_i32 add_i32 sub_one_i32)
(export-spirv-binary shaders)

(define i1 (type "i1"))
(define i32 (type "i32"))
(define index (type "index"))
(define !memref_i32 (type "memref<?xi32, strided<[?], offset: ?>>"))
(define !memref_2d_i32
  (type "memref<?x?xi32, strided<[?, ?], offset: ?>>"))

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

(define-fn is_positive_i32 ((X : i32)) -> (i1)
  (return (cmpi "4" X (constant-i32 "0"))))

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

; // Visit a return continuation in each branch.
; // CHECK-LABEL: func.func @"::foo::max_k_i32"
; // CHECK: scf.if
; // CHECK-NOT: nbdl.return_closure
; // CHECK: return
(define-fn max_k_i32 ((A : i32) (B : i32)) -> (i32)
  (with-return (K)
    (match-if (cmpi "4" A B)
      (visit K A)
      (visit K B))))

; // Pass the return continuation as the continuation of a loop.
; // CHECK-LABEL: func.func @"::foo::sum_to_k_i32"
; // CHECK: [[WHILE:%.+]]:3 = scf.while
; // CHECK: return [[WHILE]]#2 : i32
(define-fn sum_to_k_i32 ((N : i32)) -> (i32)
  (with-return (K)
    (visit sum_down N 0 K)))

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

; // Count N down to 0 exiting via the implicit else.
(define-match-fn count_down (N)
  (match-if (visit is_positive_i32 N)
    (visit count_down (visit sub_one_i32 N))))

; // Exit the loop from the else branch of a one-armed match-if.
; // CHECK-LABEL: func.func @"::foo::count_down_i32"
; // CHECK-SAME: (%arg0: i32) {
; // CHECK-NEXT: scf.while ([[N:%.+]] = %arg0) : (i32) -> i32 {
; // CHECK: [[IF:%.+]]:2 = scf.if {{%.+}} -> (i1, i32) {
; // CHECK: scf.yield %true, {{%.+}} : i1, i32
; // CHECK-NEXT: } else {
; // CHECK-NEXT: %false = arith.constant false
; // CHECK-NEXT: scf.yield %false, [[N]] : i1, i32
; // CHECK-NEXT: }
; // CHECK-NEXT: scf.condition([[IF]]#0) [[IF]]#1 : i32
; // CHECK: return
; // CHECK-NEXT: }
(define-fn count_down_i32 ((N : i32)) -> ()
  (visit count_down N))

; // Yield the flag to continue and the result from the scope of an
; // early return and run the code after it only if it continues.
; // CHECK-LABEL: func.func @"::foo::early_max_i32"
; // CHECK-SAME: (%arg0: i32, %arg1: i32) -> i32 {
; // CHECK-NEXT: [[EXIT:%.+]]:2 = scf.execute_region -> (i1, i32) {
; // CHECK: scf.yield %false, %arg0 : i1, i32
; // CHECK: [[POISON:%.+]] = ub.poison : i32
; // CHECK-NEXT: scf.yield %true, [[POISON]] : i1, i32
; // CHECK: [[MAX:%.+]] = scf.if [[EXIT]]#0 -> (i32) {
; // CHECK-NEXT: scf.yield %arg1 : i32
; // CHECK-NEXT: } else {
; // CHECK-NEXT: scf.yield [[EXIT]]#1 : i32
; // CHECK-NEXT: }
; // CHECK-NEXT: return [[MAX]] : i32
(define-fn early_max_i32 ((A : i32) (B : i32)) -> (i32)
  (match-if (cmpi "4" A B)
    (return A))
  (return B))

; // Visit a return continuation followed by more code.
; // CHECK-LABEL: func.func @"::foo::clamp_k_i32"
; // CHECK: scf.execute_region -> (i1, i32)
(define-fn clamp_k_i32 ((X : i32)) -> (i32)
  (with-return (K)
    (match-if (visit is_positive_i32 X)
      (visit K X))
    (visit K 0)))

; // Yield the flag to continue after the scope from a loop within it.
; // CHECK-LABEL: func.func @"::foo::sum_if_positive_i32"
; // CHECK: scf.execute_region -> (i1, i32)
; // CHECK: scf.while
; // CHECK-SAME: : (i32, i32) -> (i32, i32, i1, i32) {
(define-fn sum_if_positive_i32 ((N : i32)) -> (i32)
  (match-if (visit is_positive_i32 N)
    (visit sum_down N 0 (match-fn (Sum) (return Sum))))
  (return 0))

; // Exit a loop over a memref with the result of a return.
; // CHECK-LABEL: func.func @"::foo::find_positive_i32"
; // CHECK: scf.while
; // CHECK-SAME: : (index, i1, i32) -> (index, i1, i32) {
; // CHECK: arith.andi
; // CHECK: } do {
; // CHECK: memref.load
; // CHECK-NEXT: scf.execute_region -> (i1, i32)
(define-fn find_positive_i32 ((Src : !memref_i32) (A : i32)) -> (i32)
  (match-each (lift-store Src)
    (lambda (X)
      (match-if (visit is_positive_i32 X)
        (return X))))
  (return A))

; // Exit every loop over a memref with a rank > 1.
; // CHECK-LABEL: func.func @"::foo::find_positive_2d_i32"
; // CHECK: scf.while
; // CHECK: scf.while
; // CHECK-SAME: : (index, i1, i32) -> (index, i1, i32) {
(define-fn find_positive_2d_i32 ((Src : !memref_2d_i32) (A : i32)) -> (i32)
  (match-each (lift-store Src)
    (lambda (X)
      (match-if (visit is_positive_i32 X)
        (return X))))
  (return A))

; // Store X as the first element of Dest.
(define-fn store_first_i32 ((X : i32) (Dest : !memref_i32)) -> ()
  (define Zero
    (result (create-op "arith.constant"
                       (loc: 0)
                       (operands:)
                       (attributes: ("value" (attr "0" index)))
                       (result-types: index))))
  (create-op "memref.store"
             (loc: 0)
             (operands: X Dest Zero)
             (attributes:)
             (result-types:))
  (return))

; // Store each element of Src until one is not positive and store 100 if
; // every element is positive. (ie return without results)
; // CHECK-LABEL: func.func @"::foo::store_while_positive"
; // CHECK: scf.while
; // CHECK-SAME: : (index, i1) -> (index, i1) {
(define-fn store_while_positive ((Src : !memref_i32) (Dest : !memref_i32))
    -> ()
  (match-each (lift-store Src)
    (lambda (X)
      (match-if (visit is_positive_i32 X)
        (visit store_first_i32 X Dest)
        (return))))
  (visit store_first_i32 (constant-i32 "100") Dest)
  (return))

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
  SCHIR_ASSERT(foo::max_k_i32(2, 3) == 3);
  SCHIR_ASSERT(foo::max_k_i32(5, -3) == 5);
  SCHIR_ASSERT(foo::sum_to_k_i32(0) == 0);
  SCHIR_ASSERT(foo::sum_to_k_i32(4) == 10);
  // 6 + 3 + 1
  SCHIR_ASSERT(foo::sum_sums_i32(3) == 10);
  SCHIR_ASSERT(foo::sum_sums_i32(0) == 0);
  foo::count_down_i32(3);
  foo::count_down_i32(-1);
  SCHIR_ASSERT(foo::early_max_i32(3, 2) == 3);
  SCHIR_ASSERT(foo::early_max_i32(1, 2) == 2);
  SCHIR_ASSERT(foo::clamp_k_i32(4) == 4);
  SCHIR_ASSERT(foo::clamp_k_i32(-4) == 0);
  SCHIR_ASSERT(foo::sum_if_positive_i32(4) == 10);
  SCHIR_ASSERT(foo::sum_if_positive_i32(-4) == 0);

  std::vector<std::int32_t> v{-1, 0, 5, 7};
  SCHIR_ASSERT(foo::find_positive_i32(nbdl::bind_memref(v), 9) == 5);
  std::vector<std::int32_t> none{-1, 0};
  SCHIR_ASSERT(foo::find_positive_i32(nbdl::bind_memref(none), 9) == 9);
  std::vector<std::int32_t> empty;
  SCHIR_ASSERT(foo::find_positive_i32(nbdl::bind_memref(empty), 9) == 9);

  std::array<std::int32_t, 6> data{-3, -2, -1, 0, 4, 5};
  std::mdspan matrix(data.data(), std::dextents<std::intptr_t, 2>(2, 3));
  SCHIR_ASSERT(foo::find_positive_2d_i32(nbdl::bind_memref(matrix), 9) == 4);
  data[4] = 0;
  data[5] = 0;
  SCHIR_ASSERT(foo::find_positive_2d_i32(nbdl::bind_memref(matrix), 9) == 9);

  std::vector<std::int32_t> dest{0};
  std::vector<std::int32_t> stops{1, 2, -1, 4};
  foo::store_while_positive(nbdl::bind_memref(stops), nbdl::bind_memref(dest));
  SCHIR_ASSERT(dest[0] == 2);
  std::vector<std::int32_t> runs{1, 2, 3};
  foo::store_while_positive(nbdl::bind_memref(runs), nbdl::bind_memref(dest));
  SCHIR_ASSERT(dest[0] == 100);
}
