// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: FileCheck --check-prefix=LOWERED %s < %t.out
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

(export-c sum_to_i32 write_i32 count_down_ptr)
(export-c-internal is_zero_i32 add_i32 sub_one_i32 dec_is_zero_i32)
(export-cpp sum_to_int sum_down_int)

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

(define-fn is_zero_i32 ((X : i32)) -> (i1)
  (return (result (create-op "arith.cmpi"
                             (loc: 0)
                             (operands: X (constant-i32 "0"))
                             (attributes: ("predicate" (attr "0" (type "i64"))))
                             (result-types: i1)))))

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

(define-fn write_i32 ((X : i32) (Dest : !llvm.ptr)) -> ()
  (create-op "llvm.store"
             (loc: 0)
             (operands: X Dest)
             (attributes:)
             (result-types:))
  (return))

; // Sum the integers from N down to 1 adding to Acc.
; // Visit the function recursively in tail position within match-if
; // whose then region is the base case.
; // Expect the match-fn itself to be unchanged.
; // CHECK-LABEL: @sum_down(
; // CHECK-SAME: %arg0: !nbdl.store, %arg1: !nbdl.store, %arg2: !nbdl.store)
; // CHECK: "nbdl.match_if"
; // CHECK: [[SELF:%[0-9]+]] = "nbdl.func_name"() <{name = @sum_down}>
; // CHECK: [[RESULT:%[0-9]+]] = "nbdl.visit"([[SELF]],
; // CHECK-NEXT: "nbdl.discard"([[RESULT]])
(define-match-fn sum_down (N Acc Fn)
  (match-if (visit is_zero_i32 N)
    (visit Fn Acc)
    (visit sum_down (visit sub_one_i32 N)
                    (visit add_i32 Acc N)
                    Fn)))

; // Inline the visit on the recursive match-fn as a loop that carries
; // N and Acc, but not the closure Fn which is the same in every
; // iteration. Expect a continue in place of the recursive visit.
; // Inline the closure that writes to Dest.
; // CHECK-LABEL: func.func @"::foo::sum_to_i32"
; // CHECK-SAME: (%arg0: i32, %arg1: !llvm.ptr) {
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_down}>
; // CHECK-NOT: "nbdl.closure"
; // CHECK: [[N:%[0-9]+]] = "nbdl.lift_store"(%arg0)
; // CHECK-NEXT: [[ACC:%[0-9]+]] = "nbdl.lift_store"
; // CHECK-NEXT: "nbdl.loop"([[N]], [[ACC]]) <{fn = @sum_down}>
; // CHECK-NEXT: ^bb0([[N_ARG:%arg[0-9]+]]: !nbdl.store<i32>,
; // CHECK-SAME: [[ACC_ARG:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK: "nbdl.visit"({{%[0-9]+}}, [[N_ARG]])
; // CHECK-SAME: -> i1
; // CHECK: "nbdl.match_if"
; // CHECK: [[SUM:%[0-9]+]] = "nbdl.unwrap"([[ACC_ARG]])
; // CHECK-NEXT: func.call @"::foo::write_i32"([[SUM]], %arg1)
; // CHECK: [[NEXT_N:%[0-9]+]] = "nbdl.lift_store"
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK-NEXT: [[NEXT_ACC:%[0-9]+]] = "nbdl.lift_store"
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK-NEXT: "nbdl.continue"([[NEXT_N]], [[NEXT_ACC]]) <{fn = @sum_down}>
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_down}>
; // CHECK-NOT: "nbdl.closure"
; // CHECK: "nbdl.return"() : () -> ()
(define-fn sum_to_i32 ((N : i32) (Dest : !llvm.ptr)) -> ()
  (visit sum_down N 0 (match-fn (Sum) (visit write_i32 Sum Dest)))
  (return))

; // Do the same with C++ types to translate the loop to C++.
; // Define a wrapper with the exported name that inlines the match-fn
; // with each parameter lifted to a store.
; // CHECK-LABEL: @"::foo::sum_down_int.match_fn"(
; // CHECK-LABEL: func.func @"::foo::sum_down_int"
; // CHECK-SAME: (%arg0: !nbdl.unknown, %arg1: !nbdl.unknown, %arg2: !nbdl.unknown) {
; // CHECK-NEXT: [[N:%[0-9]+]] = "nbdl.lift_store"(%arg0) : (!nbdl.unknown) -> !nbdl.store
; // CHECK-NEXT: [[ACC:%[0-9]+]] = "nbdl.lift_store"(%arg1) : (!nbdl.unknown) -> !nbdl.store
; // CHECK-NEXT: "nbdl.lift_store"(%arg2) : (!nbdl.unknown) -> !nbdl.store
; // CHECK-NEXT: "nbdl.loop"([[N]], [[ACC]]) <{fn = @"::foo::sum_down_int.match_fn"}>
; // CHECK: "nbdl.continue"({{.*}}) <{fn = @"::foo::sum_down_int.match_fn"}>
(define-match-fn sum_down_int (N Acc Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn Acc)
    (visit sum_down_int (visit '|std::minus<int>{}| N '|int{1}|)
                        (visit '|std::plus<int>{}| Acc N)
                        Fn)))

; // Pass a closure that captures the sink.
; // CHECK-LABEL: func.func @"::foo::sum_to_int"
; // CHECK-NOT: "nbdl.closure"
; // CHECK: "nbdl.loop"({{.*}}) <{fn = @"::foo::sum_down_int.match_fn"}>
; // CHECK-NEXT: ^bb0({{%arg[0-9]+}}: !nbdl.store<!nbdl.cpp<"int">>,
; // CHECK-SAME: [[ACC_ARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"int">>):
; // CHECK: "nbdl.visit"(%arg1, [[ACC_ARG]])
; // CHECK: "nbdl.continue"({{.*}}) <{fn = @"::foo::sum_down_int.match_fn"}>
; // CHECK-NOT: "nbdl.closure"
; // CHECK-LABEL: func.func
(define-fn sum_to_int ((N : 'int) (Sink : 'foo::int_sink)) -> ()
  (visit sum_down_int N '|int{0}| (match-fn (Sum) (visit Sink Sum)))
  (return))

; // Decrement the i32 at P and return true if the result is zero.
(define-fn dec_is_zero_i32 ((P : !llvm.ptr)) -> (i1)
  (define Prev
    (result (create-op "llvm.load"
                       (loc: 0)
                       (operands: P)
                       (attributes:)
                       (result-types: i32))))
  (define Next
    (result (create-op "arith.subi"
                       (loc: 0)
                       (operands: Prev (constant-i32 "1"))
                       (attributes:)
                       (result-types: i32))))
  (create-op "llvm.store"
             (loc: 0)
             (operands: Next P)
             (attributes:)
             (result-types:))
  (return (result (create-op "arith.cmpi"
                             (loc: 0)
                             (operands: Next (constant-i32 "0"))
                             (attributes: ("predicate" (attr "0" (type "i64"))))
                             (result-types: i1)))))

; // Pass every argument unchanged in every recursive visit so nothing
; // is loop-carried. Let the loop still end by decrementing the counter
; // that Counter points to in the condition.
(define-match-fn count_down (Counter Fn)
  (match-if (visit dec_is_zero_i32 Counter)
    (visit Fn 7)
    (visit count_down Counter Fn)))

; // Expect no operands for the loop and the continue.
; // Inline the closure that writes to Dest.
; // CHECK-LABEL: func.func @"::foo::count_down_ptr"
; // CHECK-SAME: (%arg0: !llvm.ptr, %arg1: !llvm.ptr) {
; // CHECK-NOT: "nbdl.closure"
; // CHECK: [[COUNTER:%[0-9]+]] = "nbdl.lift_store"(%arg0)
; // CHECK: "nbdl.loop"() <{fn = @count_down}> ({
; // CHECK-NEXT: "nbdl.func_name"() <{name = @"::foo::dec_is_zero_i32"}>
; // CHECK-NEXT: "nbdl.visit"({{%[0-9]+}}, [[COUNTER]])
; // CHECK-SAME: -> i1
; // CHECK: "nbdl.match_if"
; // CHECK: func.call @"::foo::write_i32"({{%[0-9]+}}, %arg1)
; // CHECK: }, {
; // CHECK-NEXT: "nbdl.continue"() <{fn = @count_down}> : () -> ()
; // CHECK-NEXT: }) : (i1) -> ()
; // CHECK-NEXT: }) : () -> ()
(define-fn count_down_ptr ((Counter : !llvm.ptr) (Dest : !llvm.ptr)) -> ()
  (visit count_down Counter (match-fn (X) (visit write_i32 X Dest)))
  (return))

(write-lower-module)
(write-nbdl-module (finalize-module))

; // Lower the loop (via nbdl-lower) to a scf.while whose iteration
; // arguments are the loop-carried values. Yield a flag from each branch
; // of the scf.if to continue with the values for the next iteration.
; // (Expect the lower module before the Nbdl module.)
; // LOWERED-LABEL: func.func @"::foo::sum_to_i32"
; // LOWERED-SAME: (%arg0: i32, %arg1: !llvm.ptr)
; // LOWERED-NOT: memref.alloca
; // LOWERED: %c0_i32 = arith.constant 0 : i32
; // LOWERED-NEXT: scf.while ([[N:%.+]] = %arg0, [[ACC:%.+]] = %c0_i32)
; // LOWERED-SAME: : (i32, i32) -> (i32, i32) {
; // LOWERED-NEXT: [[DONE:%.+]] = func.call @"::foo::is_zero_i32"([[N]])
; // LOWERED-NEXT: [[IF:%.+]]:3 = scf.if [[DONE]] -> (i1, i32, i32) {
; // LOWERED-NEXT: func.call @"::foo::write_i32"([[ACC]], %arg1)
; // LOWERED-NEXT: %false = arith.constant false
; // LOWERED-NEXT: scf.yield %false, [[N]], [[ACC]] : i1, i32, i32
; // LOWERED-NEXT: } else {
; // LOWERED-NEXT: [[NEXT_N:%.+]] = func.call @"::foo::sub_one_i32"([[N]])
; // LOWERED-NEXT: [[NEXT_ACC:%.+]] = func.call @"::foo::add_i32"([[ACC]], [[N]])
; // LOWERED-NEXT: %true = arith.constant true
; // LOWERED-NEXT: scf.yield %true, [[NEXT_N]], [[NEXT_ACC]] : i1, i32, i32
; // LOWERED-NEXT: }
; // LOWERED-NEXT: scf.condition([[IF]]#0) [[IF]]#1, [[IF]]#2 : i32, i32
; // LOWERED-NEXT: } do {
; // LOWERED-NEXT: ^bb0([[N2:%.+]]: i32, [[ACC2:%.+]]: i32):
; // LOWERED-NEXT: scf.yield [[N2]], [[ACC2]] : i32, i32
; // LOWERED-NEXT: }
; // LOWERED-NOT: func.call @"::foo::sum_to_i32"
; // Yield only the flag from scf.if with nothing loop-carried.
; // LOWERED-LABEL: func.func @"::foo::count_down_ptr"
; // LOWERED-SAME: (%arg0: !llvm.ptr, %arg1: !llvm.ptr)
; // LOWERED-NEXT: scf.execute_region {
; // LOWERED-NEXT: scf.while : () -> () {
; // LOWERED-NEXT: [[DONE:%.+]] = func.call @"::foo::dec_is_zero_i32"(%arg0)
; // LOWERED-NEXT: [[IF:%.+]] = scf.if [[DONE]] -> (i1) {
; // LOWERED-NEXT: %c7_i32 = arith.constant 7 : i32
; // LOWERED-NEXT: func.call @"::foo::write_i32"(%c7_i32, %arg1)
; // LOWERED-NEXT: %false = arith.constant false
; // LOWERED-NEXT: scf.yield %false : i1
; // LOWERED-NEXT: } else {
; // LOWERED-NEXT: %true = arith.constant true
; // LOWERED-NEXT: scf.yield %true : i1
; // LOWERED-NEXT: }
; // LOWERED-NEXT: scf.condition([[IF]])
; // LOWERED-NEXT: } do {
; // LOWERED-NEXT: scf.yield
; // LOWERED-NEXT: }
; // LOWERED-NOT: memref.alloca
; // LOWERED-LABEL: func.func @"::foo::is_zero_i32"
}
}  // namespace foo
}  // namespace

int main() {
  // Recurse deep enough to overflow the stack
  // if it was not lowered to a loop.
  std::int32_t dest = 0;
  foo::sum_to_i32(10, &dest);
  SCHIR_ASSERT(dest == 55);
  foo::sum_to_i32(0, &dest);
  SCHIR_ASSERT(dest == 0);
  foo::sum_to_i32(65'535, &dest);
  SCHIR_ASSERT(dest == 2'147'450'880);

  // Decrement the counter once in each iteration.
  std::int32_t counter = 1000;
  dest = 0;
  foo::count_down_ptr(&counter, &dest);
  SCHIR_ASSERT(counter == 0);
  SCHIR_ASSERT(dest == 7);

  int result = 0;
  foo::sum_to_int(10, foo::int_sink{&result});
  SCHIR_ASSERT(result == 55);

  // Call the match-fn exported to C++ that visits itself recursively.
  result = 0;
  foo::sum_down_int(4, 0, [&](int Value) { result = Value; });
  SCHIR_ASSERT(result == 10);
}
