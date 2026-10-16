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

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <cstdint>

namespace {
namespace foo {

// Receive results via a concrete callable type.
struct int_sink {
  int* dest;

  void operator()(int Value) const {
    *dest = Value;
  }
};

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) attr create-op result load-dialect))

(export-c is_even_i32 repeat_sum_i32)
(export-c-internal is_zero_i32 dec_is_zero_i32 add_i32 sub_one_i32
                   write_i32)
(export-cpp repeat_sum_to_int)

(load-dialect "llvm")
(define i1 (type "i1"))
(define i32 (type "i32"))
(define !llvm.ptr (type "!llvm.ptr"))

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

(define-fn is_zero_i32 ((X : i32)) -> (i1)
  (return (is-zero X)))

(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (binary-i32 "arith.addi" A B)))

(define-fn sub_one_i32 ((X : i32)) -> (i32)
  (return (binary-i32 "arith.subi" X (constant-i32 "1"))))

(define-fn write_i32 ((X : i32) (Dest : !llvm.ptr)) -> ()
  (create-op "llvm.store"
             (loc: 0)
             (operands: X Dest)
             (attributes:)
             (result-types:))
  (return))

; // Decrement the i32 at P and return true if the result is zero.
(define-fn dec_is_zero_i32 ((P : !llvm.ptr)) -> (i1)
  (define Next
    (binary-i32 "arith.subi"
                (result (create-op "llvm.load"
                                   (loc: 0)
                                   (operands: P)
                                   (attributes:)
                                   (result-types: i32)))
                (constant-i32 "1")))
  (create-op "llvm.store"
             (loc: 0)
             (operands: Next P)
             (attributes:)
             (result-types:))
  (return (is-zero Next)))

; // Visit Fn with 1 if N is even and 0 otherwise via two functions that
; // visit each other. Declare is_odd so is_even may visit it before it
; // is defined.
(declare-match-fn is_odd)

(define-match-fn is_even (N Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn 1)
    (visit is_odd (visit sub_one_i32 N) Fn)))

(define-match-fn is_odd (N Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn 0)
    (visit is_even (visit sub_one_i32 N) Fn)))

; // Continue the loop of is_even from the inlined body of is_odd.
; // Expect no loop of is_odd since nothing continues it.
; // Inline the closure that writes to Dest.
; // (Skip the lowered module.)
; // CHECK-LABEL: @is_odd(
; // CHECK-LABEL: func.func @"::foo::is_even_i32"
; // CHECK-NOT: fn = @is_odd
; // CHECK-NOT: "nbdl.closure"
; // CHECK: "nbdl.loop"({{%[0-9]+}}) <{fn = @is_even}> ({
; // CHECK-NEXT: ^bb0([[N:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.match_if"
; // CHECK: func.call @"::foo::write_i32"({{%[0-9]+}}, %arg1)
; // CHECK: "nbdl.match_if"
; // CHECK: func.call @"::foo::write_i32"({{%[0-9]+}}, %arg1)
; // CHECK: "nbdl.continue"({{%[0-9]+}}) <{fn = @is_even}>
; // CHECK-NOT: fn = @is_odd
; // CHECK: "nbdl.return"
(define-fn is_even_i32 ((N : i32) (Dest : !llvm.ptr)) -> ()
  (visit is_even N (match-fn (Result) (visit write_i32 Result Dest)))
  (return))

; // Sum the integers from N down to 1 adding to Acc.
; // Visit Fn with the sum.
(define-match-fn sum_down (N Acc Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn Acc)
    (visit sum_down (visit sub_one_i32 N)
                    (visit add_i32 Acc N)
                    Fn)))

; // Add the sum of 1 to 4 to Acc until decrementing the i32 at Counter
; // results in zero. Then visit Fn with Acc.
; // Visit repeat_sum from sum_down via a closure.
(define-match-fn repeat_sum (Acc Counter Fn)
  (match-if (visit dec_is_zero_i32 Counter)
    (visit Fn Acc)
    (visit sum_down 4 Acc
           (match-fn (Sum) (visit repeat_sum Sum Counter Fn)))))

; // Expect the match-fn itself to keep the closure.
; // Continue the loop of repeat_sum from the base case of the loop of
; // sum_down which is within it.
; // CHECK-LABEL: @repeat_sum(
; // CHECK: "nbdl.closure"
; // CHECK-LABEL: func.func @"::foo::repeat_sum_i32"
; // CHECK-NOT: "nbdl.closure"
; // CHECK: "nbdl.loop"({{%[0-9]+}}) <{fn = @repeat_sum}> ({
; // CHECK-NEXT: ^bb0([[ACC:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.match_if"
; // CHECK: "nbdl.loop"({{%[0-9]+}}, [[ACC]]) <{fn = @sum_down}> ({
; // CHECK-NEXT: ^bb0({{%arg[0-9]+}}: !nbdl.store<i32>,
; // CHECK-SAME: [[INNER_ACC:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.match_if"
; // CHECK-NEXT: ^bb0
; // CHECK-NEXT: "nbdl.continue"([[INNER_ACC]]) <{fn = @repeat_sum}>
; // CHECK-NEXT: }, {
; // CHECK: "nbdl.continue"({{%[0-9]+}}, {{%[0-9]+}}) <{fn = @sum_down}>
; // CHECK-NOT: "nbdl.closure"
; // CHECK: "nbdl.return"
(define-fn repeat_sum_i32 ((Counter : !llvm.ptr)) -> ()
  (visit repeat_sum 0 Counter
         (match-fn (Acc) (visit write_i32 Acc Counter)))
  (return))

; // Do the same with C++ types to translate the loops to C++.
(define-match-fn sum_down_int (N Acc Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn Acc)
    (visit sum_down_int (visit '|std::minus<int>{}| N '|int{1}|)
                        (visit '|std::plus<int>{}| Acc N)
                        Fn)))

(define-match-fn repeat_sum_int (Count Acc Sink)
  (match-if (visit '|std::equal_to<int>{}| Count '|int{0}|)
    (visit Sink Acc)
    (visit sum_down_int '|int{4}|
                        Acc
                        (match-fn (Sum)
                          (visit repeat_sum_int
                                 (visit '|std::minus<int>{}| Count '|int{1}|)
                                 Sum
                                 Sink)))))

(define-fn repeat_sum_to_int ((Count : 'int) (Sink : 'foo::int_sink)) -> ()
  (visit repeat_sum_int Count '|int{0}| Sink)
  (return))

(write-lowered-module)
(finalize-module)
(write-nbdl-module)

; // Continue the outer loop from the inner loop by exiting the inner loop
; // with the flag and values of the outer loop as its results.
; // (Expect the lowered module before the Nbdl module.)
; // LOWERED-LABEL: func.func @"::foo::is_even_i32"
; // LOWERED: scf.while ({{%.+}} = %arg0) : (i32) -> i32 {
; // LOWERED-NOT: scf.while
; // LOWERED-LABEL: func.func @"::foo::repeat_sum_i32"
; // LOWERED: scf.while ([[ACC:%.+]] = %c0_i32) : (i32) -> i32 {
; // LOWERED: [[INNER:%.+]]:4 = scf.while ([[N:%.+]] = %c4_i32, [[INNER_ACC:%.+]] = [[ACC]])
; // LOWERED-SAME: : (i32, i32) -> (i32, i32, i1, i32) {
; // LOWERED: scf.if {{%.+}} -> (i1, i32, i32, i1, i32) {
; // LOWERED-NEXT: [[FALSE:%.+]] = arith.constant false
; // LOWERED-NEXT: [[TRUE:%.+]] = arith.constant true
; // LOWERED-NEXT: scf.yield [[FALSE]], [[N]], [[INNER_ACC]], [[TRUE]], [[INNER_ACC]] : i1, i32, i32, i1, i32
; // LOWERED: scf.yield [[INNER]]#2, [[INNER]]#3 : i1, i32
; // LOWERED-NOT: memref.alloca
; // LOWERED-LABEL: func.func @"::foo::is_zero_i32"
}
}  // namespace foo
}  // namespace

int main() {
  // Recurse deep enough to overflow the stack
  // if it was not lowered to a loop.
  std::int32_t dest = -1;
  foo::is_even_i32(10, &dest);
  SCHIR_ASSERT(dest == 1);
  foo::is_even_i32(7, &dest);
  SCHIR_ASSERT(dest == 0);
  foo::is_even_i32(1'000'001, &dest);
  SCHIR_ASSERT(dest == 0);

  // Add 10 for each decrement before the counter is zero.
  std::int32_t counter = 4;
  foo::repeat_sum_i32(&counter);
  SCHIR_ASSERT(counter == 30);
  counter = 100'000;
  foo::repeat_sum_i32(&counter);
  SCHIR_ASSERT(counter == 999'990);

  int result = -1;
  foo::repeat_sum_to_int(3, foo::int_sink{&result});
  SCHIR_ASSERT(result == 30);
  foo::repeat_sum_to_int(0, foo::int_sink{&result});
  SCHIR_ASSERT(result == 0);
}
