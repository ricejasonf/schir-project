// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s > %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck --check-prefix=LOWERED %s < %t.mlir

// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <nbdl/bind_memref.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <cstdint>
#include <vector>

namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) attr create-op result load-dialect))

(export-c count_up_then_sum sum_each)
(export-c-internal is_zero_i32 dec_is_zero_i32 add_i32 add_one_i32
                   sub_one_i32 accumulate_i32)

(load-dialect "llvm")
(load-dialect "memref")
(define i1 (type "i1"))
(define i32 (type "i32"))
(define !llvm.ptr (type "!llvm.ptr"))
(define !memref_i32 (type "memref<?xi32, strided<[?], offset: ?>>"))

(define (constant-i32 Value)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr Value i32)))
                     (result-types: i32))))

(define (is-zero X)
  (result (create-op "arith.cmpi"
                     (loc: 0)
                     (operands: X (constant-i32 "0"))
                     (attributes: ("predicate" (attr "0" (type "i64"))))
                     (result-types: i1))))

(define (binary-i32 Name A B)
  (result (create-op Name
                     (loc: 0)
                     (operands: A B)
                     (attributes:)
                     (result-types: i32))))

(define (load-i32 P)
  (result (create-op "llvm.load"
                     (loc: 0)
                     (operands: P)
                     (attributes:)
                     (result-types: i32))))

(define (store-i32 X P)
  (create-op "llvm.store"
             (loc: 0)
             (operands: X P)
             (attributes:)
             (result-types:)))

(define-fn is_zero_i32 ((X : i32)) -> (i1)
  (return (is-zero X)))

(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (binary-i32 "arith.addi" A B)))

(define-fn add_one_i32 ((X : i32)) -> (i32)
  (return (binary-i32 "arith.addi" X (constant-i32 "1"))))

(define-fn sub_one_i32 ((X : i32)) -> (i32)
  (return (binary-i32 "arith.subi" X (constant-i32 "1"))))

; // Decrement the i32 at P and return true if the result is zero.
(define-fn dec_is_zero_i32 ((P : !llvm.ptr)) -> (i1)
  (define Next (binary-i32 "arith.subi" (load-i32 P) (constant-i32 "1")))
  (store-i32 Next P)
  (return (is-zero Next)))

; // Add X to the i32 at Dest.
(define-fn accumulate_i32 ((X : i32) (Dest : !llvm.ptr)) -> ()
  (store-i32 (binary-i32 "arith.addi" (load-i32 Dest) X) Dest)
  (return))

; // Sum the integers from N down to 1 adding to Acc.
(define-match-fn sum_down (N Acc Dest Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn Acc Dest)
    (visit sum_down (visit sub_one_i32 N)
                    (visit add_i32 Acc N)
                    Dest
                    Fn)))

; // Count N up while decrementing the counter at Counter.
; // Visit sum_down in the base case to inline it as a loop as well.
(define-match-fn count_up (Counter N Dest)
  (match-if (visit dec_is_zero_i32 Counter)
    (visit sum_down N 0 Dest accumulate_i32)
    (visit count_up Counter (visit add_one_i32 N) Dest)))

; // Nest the loop of sum_down in the loop of count_up.
; // Expect each continue to be for its own loop.
; // CHECK-LABEL: func.func @"::foo::count_up_then_sum"(%arg0: !llvm.ptr, %arg1: !llvm.ptr) {
; // CHECK: "nbdl.loop"({{%[0-9]+}}) ({
; // CHECK-NEXT: ^bb0([[OUTER_N:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.match_if"
; // CHECK: "nbdl.loop"([[OUTER_N]], {{%[0-9]+}}) ({
; // CHECK-NEXT: ^bb0({{%arg[0-9]+}}: !nbdl.store<i32>,
; // CHECK-SAME: {{%arg[0-9]+}}: !nbdl.store<i32>):
; // CHECK: func.call @"::foo::accumulate_i32"
; // CHECK: "nbdl.continue"({{%[0-9]+}}, {{%[0-9]+}})
; // CHECK-NEXT: }) : (i1) -> ()
; // CHECK-NEXT: }) : (!nbdl.store<i32>, !nbdl.store<i32>) -> ()
; // CHECK-NEXT: }, {
; // CHECK: "nbdl.continue"({{%[0-9]+}})
; // CHECK-NEXT: }) : (i1) -> ()
; // CHECK-NEXT: }) : (!nbdl.store<i32>) -> ()
(define-fn count_up_then_sum ((Counter : !llvm.ptr) (Dest : !llvm.ptr)) -> ()
  (visit count_up Counter 0 Dest)
  (return))

; // Run a loop for each element of the memref.
; // CHECK-LABEL: func.func @"::foo::sum_each"({{.*}}, %arg1: !llvm.ptr) {
; // CHECK: "nbdl.match_each"
; // CHECK-NEXT: ^bb0([[X:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.loop"([[X]], {{%[0-9]+}})
(define-fn sum_each ((Src : !memref_i32) (Dest : !llvm.ptr)) -> ()
  (match-each (lift-store Src)
    (lambda (X)
      (visit sum_down X 0 Dest accumulate_i32)))
  (return))

(write-lowered-module)
(finalize-module)
(write-nbdl-module)

; // Lower each loop to a scf.while with the loop-carried values as its
; // iteration arguments so no memory is allocated for either loop.
; // Let the inner loop end the outer loop like any other terminator.
; // LOWERED-LABEL: func.func @"::foo::count_up_then_sum"
; // LOWERED-NOT: memref.alloca
; // LOWERED: scf.while ([[OUTER_N:%.+]] = %c0_i32) : (i32) -> i32 {
; // LOWERED-NEXT: [[DONE:%.+]] = func.call @"::foo::dec_is_zero_i32"(%arg0)
; // LOWERED-NEXT: [[OUTER_IF:%.+]]:2 = scf.if [[DONE]] -> (i1, i32) {
; // LOWERED-NEXT: [[ZERO:%.+]] = arith.constant 0 : i32
; // LOWERED-NEXT: scf.while ([[N:%.+]] = [[OUTER_N]], [[ACC:%.+]] = [[ZERO]])
; // LOWERED-SAME: : (i32, i32) -> (i32, i32) {
; // LOWERED-NEXT: [[INNER_DONE:%.+]] = func.call @"::foo::is_zero_i32"([[N]])
; // LOWERED-NEXT: [[INNER_IF:%.+]]:3 = scf.if [[INNER_DONE]] -> (i1, i32, i32) {
; // LOWERED-NEXT: func.call @"::foo::accumulate_i32"([[ACC]], %arg1)
; // LOWERED-NEXT: [[FALSE:%.+]] = arith.constant false
; // LOWERED-NEXT: scf.yield [[FALSE]], [[N]], [[ACC]] : i1, i32, i32
; // LOWERED-NEXT: } else {
; // LOWERED: scf.yield %true, {{%.+}}, {{%.+}} : i1, i32, i32
; // LOWERED-NEXT: }
; // LOWERED-NEXT: scf.condition([[INNER_IF]]#0) [[INNER_IF]]#1, [[INNER_IF]]#2 : i32, i32
; // LOWERED-NEXT: } do {
; // LOWERED: }
; // LOWERED-NEXT: %false = arith.constant false
; // LOWERED-NEXT: scf.yield %false, [[OUTER_N]] : i1, i32
; // LOWERED-NEXT: } else {
; // LOWERED-NEXT: [[NEXT_N:%.+]] = func.call @"::foo::add_one_i32"([[OUTER_N]])
; // LOWERED-NEXT: %true = arith.constant true
; // LOWERED-NEXT: scf.yield %true, [[NEXT_N]] : i1, i32
; // LOWERED-NEXT: }
; // LOWERED-NEXT: scf.condition([[OUTER_IF]]#0) [[OUTER_IF]]#1 : i32
; // LOWERED-NOT: memref.alloca
; // LOWERED-LABEL: func.func @"::foo::sum_each"
; // LOWERED-NOT: memref.alloca
; // LOWERED: scf.for
; // LOWERED-NEXT: [[X:%.+]] = memref.load %arg0
; // LOWERED-NEXT: [[ZERO:%.+]] = arith.constant 0 : i32
; // LOWERED-NEXT: scf.while ({{%.+}} = [[X]], {{%.+}} = [[ZERO]])
; // LOWERED-SAME: : (i32, i32) -> (i32, i32) {
; // LOWERED-NOT: memref.alloca
; // LOWERED-LABEL: func.func @"::foo::is_zero_i32"
}
}  // namespace foo
}  // namespace

int main() {
  // Count N up to 4 then sum 4 + 3 + 2 + 1.
  std::int32_t counter = 5;
  std::int32_t dest = 0;
  foo::count_up_then_sum(&counter, &dest);
  SCHIR_ASSERT(counter == 0);
  SCHIR_ASSERT(dest == 10);

  // Run a loop for each element.
  std::vector<std::int32_t> src(100'000, 3);
  src.push_back(4);
  dest = 0;
  foo::sum_each(nbdl::bind_memref(src), &dest);
  SCHIR_ASSERT(dest == 100'000 * 6 + 10);
}
