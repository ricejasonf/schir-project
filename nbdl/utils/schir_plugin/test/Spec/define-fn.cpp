// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

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

struct my_struct {
  int value = 0;
};

// Receive a result via a free function.
int global_result = 0;
void set_global_result(int Value) {
  global_result = Value;
}

// A concrete callable type to receive results.
struct int_sink {
  int* dest;

  void operator()(int Value) const {
    *dest = Value;
  }
};

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) create-op result load-dialect))

(export-c add_i32 add_one_i32 write_i32 sum_to_ptr)
(export-cpp context
            add_ints
            get_value
            add_value
            sum_foo_bar_i32
            store_sum
            set_result
            store_sum_func_name)

(define-context context (arg1 arg2)
  (member: '.foo 'int (init-args: arg1))
  (member: '.bar 'int (init-args: arg2)))

; // Types may be specified explicitly as mlir types.
; // Parameters are not stores, and neither are the results
; // of constexpr and visit.
; // CHECK-LABEL: func.func @"::foo::add_ints"
; // CHECK-SAME: (%arg0: !nbdl.cpp<"int">, %arg1: !nbdl.cpp<"int">)
; // CHECK-SAME: -> !nbdl.cpp<"int">
; // CHECK: [[PLUS:%[0-9]+]] = "nbdl.constexpr"() <{expr = "std::plus<int>{}"}>
; // CHECK-SAME: () -> !nbdl.cpp<"std::plus<int>">
; // CHECK-NEXT: [[SUM:%[0-9]+]] = "nbdl.visit"([[PLUS]], %arg0, %arg1)
; // CHECK-SAME: -> !nbdl.cpp<"int">
; // CHECK-NEXT: "nbdl.return"([[SUM]])
(define-fn add_ints ((A : (!cpp 'int)) (B : (!cpp 'int))) -> ((!cpp 'int))
  (return (visit '|std::plus<int>{}| A B)))

; // Visit a member of a parameter with a concrete C++ type.
; // The parameter is lifted to a store to use it as the root of a path.
; // CHECK-LABEL: func.func @"::foo::get_value"
; // CHECK-SAME: (%arg0: !nbdl.cpp<"foo::my_struct">,
; // CHECK-SAME: %arg1: !nbdl.cpp<"foo::int_sink">) {
; // CHECK: [[S:%[0-9]+]] = "nbdl.lift_store"(%arg0)
; // CHECK-SAME: (!nbdl.cpp<"foo::my_struct">)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::my_struct">>
; // CHECK: [[VALUE:%[0-9]+]] = "nbdl.get"([[S]],
; // CHECK: "nbdl.visit"(%arg1, [[VALUE]])
(define-fn get_value ((S : 'foo::my_struct) (Sink : 'foo::int_sink)) -> ()
  (visit Sink (get (lift-store S) '.value))
  (return))

; // Visit a define-fn within a define-fn.
; // CHECK-LABEL: func.func @"::foo::add_value"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @"::foo::add_ints"}>
; // CHECK: [[RESULT:%[0-9]+]] = "nbdl.visit"([[FN]],
; // CHECK-SAME: -> !nbdl.cpp<"int">
; // CHECK-NEXT: "nbdl.return"([[RESULT]])
(define-fn add_value ((S : 'foo::my_struct) (X : 'int)) -> ('int)
  (return (visit add_ints (get (lift-store S) '.value) X)))

; // Functions exported via export-c have the unqualified symbol name
; // and are lowered to LLVM.
; // CHECK-LABEL: func.func @add_i32
; // CHECK-SAME: (%arg0: i32, %arg1: i32) -> i32
; // CHECK-NEXT: [[SUM:%[0-9]+]] = arith.addi %arg0, %arg1 : i32
; // CHECK-NEXT: "nbdl.return"([[SUM]]) : (i32) -> ()
(define i32 (type "i32"))
(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (result (create-op "arith.addi"
                             (loc: 0)
                             (operands: A B)
                             (attributes:)
                             (result-types: i32)))))

; // Visit an export-c function from an export-c function.
; // Literals are not stores.
; // CHECK-LABEL: func.func @add_one_i32
; // CHECK-SAME: (%arg0: i32) -> i32
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @add_i32}>
; // CHECK-NEXT: [[ONE:%[0-9]+]] = "nbdl.literal"() <{value = 1 : i32}>
; // CHECK-SAME: () -> i32
; // CHECK-NEXT: [[RESULT:%[0-9]+]] = "nbdl.visit"([[FN]], %arg0, [[ONE]])
; // CHECK-SAME: -> i32
; // CHECK-NEXT: "nbdl.return"([[RESULT]])
(define-fn add_one_i32 ((A : i32)) -> (i32)
  (return (visit add_i32 A 1)))

; // Visit a define-fn within a match-fn.
; // This is not exported since it is only inlined via store_sum.
; // CHECK-LABEL: func.func @sum_foo_bar(
; // CHECK: "nbdl.visit"
; // CHECK-SAME: -> !nbdl.cpp<"int">
(define-match-fn sum_foo_bar (Ctx Fn)
  (match-params ((Foo : 'int (get Ctx '.foo))
                 (Bar : 'int (get Ctx '.bar)))
    (visit Fn (visit add_ints Foo Bar))))

; // Visit a define-fn with MLIR types within a match-fn.
; // CHECK-LABEL: func.func @"::foo::sum_foo_bar_i32"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @add_i32}>
; // CHECK: "nbdl.visit"([[FN]],
; // CHECK-SAME: <{validCppCrossMap}>
; // CHECK-SAME: -> i32
(define-match-fn sum_foo_bar_i32 (Ctx Fn)
  (match-params ((Foo : 'int (get Ctx '.foo))
                 (Bar : 'int (get Ctx '.bar)))
    (visit Fn (visit add_i32 Foo Bar))))

; // Visit a match-fn (which matches stuff within its body)
; // from a define-fn with no results.
; // The match-fn is inlined lifting the arguments to stores.
; // The results of get are then inferred so the matches are inlined.
; // CHECK-LABEL: func.func @"::foo::store_sum"
; // CHECK-SAME: (%arg0: !nbdl.cpp<"foo::context">,
; // CHECK-SAME: %arg1: !nbdl.cpp<"foo::int_sink">) {
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_foo_bar}>
; // CHECK: [[CTX:%[0-9]+]] = "nbdl.lift_store"(%arg0)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::context">>
; // CHECK-NEXT: [[SINK:%[0-9]+]] = "nbdl.lift_store"(%arg1)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::int_sink">>
; // CHECK: "nbdl.get"([[CTX]],
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK-NOT: "nbdl.match"
; // CHECK: "nbdl.visit"([[SINK]],
; // CHECK: "nbdl.return"() : () -> ()
(define-fn store_sum ((Ctx : 'foo::context) (Sink : 'foo::int_sink)) -> ()
  (visit sum_foo_bar Ctx Sink)
  (return))

; // A define-fn to be used as a continuation.
(define-fn set_result ((X : 'int)) -> ()
  (visit 'foo::set_global_result X)
  (return))

; // Visit a match-fn with a func_name as the continuation.
; // The parameter type is the store defined by define-context.
; // The func_name is lifted to a store when the match-fn is inlined,
; // but the callee is still visible through the store so the visit
; // of the continuation is lowered to a call.
; // CHECK-LABEL: func.func @"::foo::store_sum_func_name"
; // CHECK-SAME: (%arg0: !nbdl.cpp<"foo::context">) {
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_foo_bar}>
; // CHECK: [[SUM:%[0-9]+]] = "nbdl.visit"
; // CHECK-SAME: -> !nbdl.cpp<"int">
; // CHECK-NEXT: func.call @"::foo::set_result"([[SUM]])
; // CHECK-SAME: (!nbdl.cpp<"int">) -> ()
; // CHECK: "nbdl.return"() : () -> ()
(define-fn store_sum_func_name ((Ctx : context)) -> ()
  (visit sum_foo_bar Ctx set_result)
  (return))

; // Visit a match-fn with a func_name as the continuation
; // from an export-c function lowered to LLVM.
; // The continuation writes its result through a pointer.
(load-dialect "llvm")
(define !llvm.ptr (type "!llvm.ptr"))
(define-fn write_i32 ((X : i32) (Dest : !llvm.ptr)) -> ()
  (create-op "llvm.store"
             (loc: 0)
             (operands: X Dest)
             (attributes:)
             (result-types:))
  (return))

(define-match-fn sum_to (A B Dest Fn)
  (visit Fn (visit add_i32 A B) Dest))

; // The continuation is lowered to a call where the
; // pointer is unwrapped from its lifted store.
; // CHECK-LABEL: func.func @sum_to_ptr
; // CHECK-SAME: (%arg0: i32, %arg1: i32, %arg2: !llvm.ptr) {
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_to}>
; // CHECK: [[SUM:%[0-9]+]] = "nbdl.visit"
; // CHECK-SAME: -> i32
; // CHECK: [[DEST:%[0-9]+]] = "nbdl.unwrap"
; // CHECK-SAME: (!nbdl.store<!llvm.ptr>) -> !llvm.ptr
; // CHECK-NEXT: func.call @write_i32([[SUM]], [[DEST]])
; // CHECK: "nbdl.return"() : () -> ()
(define-fn sum_to_ptr ((A : i32) (B : i32) (Dest : !llvm.ptr)) -> ()
  (visit sum_to A B Dest write_i32)
  (return))

(finalize-module)
(write-nbdl-module)

}
}  // namespace foo
}  // namespace

int main() {
  SCHIR_ASSERT(foo::add_ints(3, 4) == 7);
  {
    int value = 0;
    foo::get_value(foo::my_struct{42}, foo::int_sink{&value});
    SCHIR_ASSERT(value == 42);
  }
  SCHIR_ASSERT(foo::add_value(foo::my_struct{42}, 8) == 50);
  // Functions exported via export-c are declared extern "C".
  SCHIR_ASSERT(foo::add_i32(5, 6) == 11);
  SCHIR_ASSERT(foo::add_i32(-5, 3) == -2);
  SCHIR_ASSERT(foo::add_one_i32(41) == 42);
  {
    std::int32_t dest = 0;
    foo::write_i32(7, &dest);
    SCHIR_ASSERT(dest == 7);
    foo::sum_to_ptr(20, 22, &dest);
    SCHIR_ASSERT(dest == 42);
  }

  // Functions are normal functions with the specified types.
  static_assert(std::same_as<decltype(foo::add_ints), int(int, int)>);
  static_assert(std::same_as<decltype(foo::get_value),
                             void(foo::my_struct, foo::int_sink)>);
  static_assert(std::same_as<decltype(foo::store_sum),
                             void(foo::context, foo::int_sink)>);

  auto ctx = foo::context(6, 7);

  int sum = 0;
  foo::sum_foo_bar_i32(ctx, [&](int v) {
    sum = v;
  });
  SCHIR_ASSERT(sum == 13);

  sum = 0;
  foo::store_sum(ctx, foo::int_sink{&sum});
  SCHIR_ASSERT(sum == 13);

  foo::global_result = 0;
  foo::store_sum_func_name(ctx);
  SCHIR_ASSERT(foo::global_result == 13);
}
