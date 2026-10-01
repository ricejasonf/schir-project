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

// Defined via define-fn with export-c.
extern "C" std::int32_t add_i32(std::int32_t, std::int32_t);
extern "C" std::int32_t add_one_i32(std::int32_t);

namespace {
namespace foo {

struct my_struct {
  int value = 0;
};

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
        (only (schir mlir) create-op result))

(export-c add_i32 add_one_i32)
(export-cpp context
            add_ints
            get_value
            add_value
            sum_foo_bar_i32
            store_sum)

(define-context context (arg1 arg2)
  (member: '.foo 'int (init-args: arg1))
  (member: '.bar 'int (init-args: arg2)))

; // Types may be specified explicitly as mlir types.
; // CHECK-LABEL: func.func @"::foo::add_ints"
; // CHECK-SAME: (%arg0: !nbdl.store<!nbdl.cpp<"int">>,
; // CHECK-SAME: %arg1: !nbdl.store<!nbdl.cpp<"int">>)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK: [[SUM:%[0-9]+]] = "nbdl.visit"
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK-NEXT: "nbdl.return"([[SUM]])
(define-fn add_ints ((A : (!cpp 'int)) (B : (!cpp 'int))) -> ((!cpp 'int))
  (return (visit '|std::plus<int>{}| A B)))

; // Get a member of a parameter with a concrete C++ type.
; // CHECK-LABEL: func.func @"::foo::get_value"
; // CHECK-SAME: (%arg0: !nbdl.store<!nbdl.cpp<"foo::my_struct">>)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK: [[VALUE:%[0-9]+]] = "nbdl.get"(%arg0,
; // CHECK-NEXT: "nbdl.return"([[VALUE]])
(define-fn get_value ((S : 'foo::my_struct)) -> ('int)
  (return (get S '.value)))

; // Visit a define-fn within a define-fn.
; // CHECK-LABEL: func.func @"::foo::add_value"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @"::foo::add_ints"}>
; // CHECK: [[RESULT:%[0-9]+]] = "nbdl.visit"([[FN]],
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK-NEXT: "nbdl.return"([[RESULT]])
(define-fn add_value ((S : 'foo::my_struct) (X : 'int)) -> ('int)
  (return (visit add_ints (get S '.value) X)))

; // Functions exported via export-c have the unqualified symbol name
; // and are lowered to LLVM with the contained types of the stores.
; // CHECK-LABEL: func.func @add_i32
; // CHECK-SAME: (%arg0: !nbdl.store<i32>, %arg1: !nbdl.store<i32>)
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK: [[A:%[0-9]+]] = "nbdl.unwrap"(%arg0)
; // CHECK-NEXT: [[B:%[0-9]+]] = "nbdl.unwrap"(%arg1)
; // CHECK-NEXT: [[SUM:%[0-9]+]] = arith.addi [[A]], [[B]] : i32
; // CHECK-NEXT: "nbdl.return"([[SUM]]) : (i32) -> ()
(define i32 (type "i32"))
(define (unwrap-i32 Store)
  (result (create-op "nbdl.unwrap"
                     (loc: Store)
                     (operands: Store)
                     (attributes:)
                     (result-types: i32))))
(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (result (create-op "arith.addi"
                             (loc: 0)
                             (operands: (unwrap-i32 A) (unwrap-i32 B))
                             (attributes:)
                             (result-types: i32)))))

; // Visit an export-c function from an export-c function.
; // CHECK-LABEL: func.func @add_one_i32
; // CHECK-SAME: (%arg0: !nbdl.store<i32>) -> !nbdl.store<i32>
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @add_i32}>
; // CHECK: [[RESULT:%[0-9]+]] = "nbdl.visit"([[FN]], %arg0,
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK-NEXT: "nbdl.return"([[RESULT]])
(define-fn add_one_i32 ((A : i32)) -> (i32)
  (return (visit add_i32 A 1)))

; // Visit a define-fn within a match-fn.
; // This is not exported since it is only inlined via store_sum.
; // CHECK-LABEL: func.func @sum_foo_bar(
; // CHECK: "nbdl.visit"
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
(define-match-fn sum_foo_bar (Ctx Fn)
  (match-params ((Foo : 'int (get Ctx '.foo))
                 (Bar : 'int (get Ctx '.bar)))
    (visit Fn (visit add_ints Foo Bar))))

; // Visit a define-fn with MLIR types within a match-fn.
; // CHECK-LABEL: func.func @"::foo::sum_foo_bar_i32"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @add_i32}>
; // CHECK: "nbdl.visit"([[FN]],
; // CHECK-SAME: <{validCppCrossMap}>
(define-match-fn sum_foo_bar_i32 (Ctx Fn)
  (match-params ((Foo : 'int (get Ctx '.foo))
                 (Bar : 'int (get Ctx '.bar)))
    (visit Fn (visit add_i32 Foo Bar))))

; // Visit a match-fn (which matches stuff within its body)
; // from a define-fn with no results.
; // The match-fn is inlined.
; // CHECK-LABEL: func.func @"::foo::store_sum"
; // CHECK-SAME: (%arg0: !nbdl.store<!nbdl.cpp<"foo::context">>,
; // CHECK-SAME: %arg1: !nbdl.store<!nbdl.cpp<"foo::int_sink">>) {
; // CHECK-NOT: "nbdl.func_name"() <{name = @sum_foo_bar}>
; // CHECK: "nbdl.match"
; // CHECK: "nbdl.match"
; // CHECK: "nbdl.return"() : () -> ()
(define-fn store_sum ((Ctx : 'foo::context) (Sink : 'foo::int_sink)) -> ()
  (visit sum_foo_bar Ctx Sink)
  (return))

(finalize-module)
(write-nbdl-module)

}
}  // namespace foo
}  // namespace

int main() {
  SCHIR_ASSERT(foo::add_ints(3, 4) == 7);
  SCHIR_ASSERT(foo::get_value(foo::my_struct{42}) == 42);
  SCHIR_ASSERT(foo::add_value(foo::my_struct{42}, 8) == 50);
  SCHIR_ASSERT(add_i32(5, 6) == 11);
  SCHIR_ASSERT(add_i32(-5, 3) == -2);
  SCHIR_ASSERT(add_one_i32(41) == 42);

  // Concrete parameter types are constrained.
  static_assert(std::invocable<decltype(foo::add_ints), int, int>);
  static_assert(!std::invocable<decltype(foo::add_ints), long, int>);
  static_assert(std::same_as<decltype(foo::add_ints(1, 2)), int>);

  auto ctx = foo::context(6, 7);

  int sum = 0;
  foo::sum_foo_bar_i32(ctx, [&](int v) {
    sum = v;
  });
  SCHIR_ASSERT(sum == 13);

  sum = 0;
  foo::store_sum(ctx, foo::int_sink{&sum});
  SCHIR_ASSERT(sum == 13);
}
