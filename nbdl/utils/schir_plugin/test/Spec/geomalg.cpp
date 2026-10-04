// RUN: clang++ -std=c++26 \
// RUN:   -I %schir_module_path \
// RUN:   -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

// RUN: clang++ -std=c++26 \
// RUN:   -I %schir_module_path \
// RUN:   -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

// COM_RUNz: clang++ -std=c++26 \
// COM_RUNz:   -DTEST_INVALID_PARAMS=1 \
// COM_RUNz:   -I %schir_module_path \
// COM_RUNz:   -I %nbdl_module_path \
// COM_RUNz:   -I %geomalg_module_path \
// COM_RUNz:   -fplugin=SchirClang.so \
// COM_RUNz:   -fsyntax-only -Xclang -verify %s

// Copyright Jason Rice 2026

#include <geomalg/nbdl.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace {
namespace foo {

#pragma schir_scheme
{
  (import (nbdl spec)
          (nbdl spec geomalg)
          (geomalg base))

  (export-c test_dot test_add cancel_vec3)
  (export-cpp test_test_dot test_test_call)

  ; // Functions with geomalg operations exported to C are lowered to LLVM
  ; // by the lowering pass registered by (nbdl spec geomalg).
  ; // CHECK-LABEL: func.func @"::foo::test_dot"
  ; // CHECK-SAME: -> !geomalg.blade<0>
  ; // CHECK: "geomalg.dot"
  ; // CHECK: "nbdl.return"
  (define-fn test_dot ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (dot A B)))

  ; // CHECK-LABEL: func.func @"::foo::test_add"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: "geomalg.expand"
  ; // CHECK: "nbdl.return"
  (define-fn test_add ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (sum A B)))

  ; // The trivector term of the reflection is
  ; // simplified away before the result type is inferred.
  ; // CHECK-LABEL: func.func @test_reflect
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NOT: "geomalg.vprod"
  ; // CHECK: "nbdl.return"
  (define-fn test_reflect ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (vprod A B)))

  ; // CHECK-LABEL: @"::foo::test_test_dot"
  ; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @"::foo::test_dot"}>
  ; // CHECK: "nbdl.visit"([[FN]],
  ; // CHECK-SAME: <{validCppCrossMap}>
  (define-match-fn test_test_dot (Store Fn)
    (match-params ((A : 'geomalg::vec3 (get Store '.a))
                   (B : 'geomalg::vec3 (get Store '.b)))
      (visit Fn (visit test_dot A B))))

  ; // Visit test_dot with MLIR typed results lowering to func.call.
  ; // The results of visit are not stores so they need no unwrap.
  ; // CHECK-LABEL: @"::foo::test_test_call"
  ; // CHECK: [[SUM1:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: <{validCppCrossMap}>
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: [[SUM2:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: <{validCppCrossMap}>
  ; // CHECK-NOT: "nbdl.unwrap"
  ; // CHECK: func.call @"::foo::test_dot"([[SUM1]], [[SUM2]])
  (define-match-fn test_test_call (Store Fn)
    (match-params ((A : 'geomalg::vec3 (get Store '.a))
                   (B : 'geomalg::vec3 (get Store '.b)))
      (visit test_dot (visit test_add A B) (visit test_add B A))))

  ; // CHECK-LABEL: @test_call_dot(
  ; // CHECK: ^bb0([[A:%arg[0-9]+]]: !nbdl.store<!geomalg.multivector<<1>, <2>, <4>>>):
  ; // CHECK: ^bb0([[B:%arg[0-9]+]]: !nbdl.store<!geomalg.multivector<<1>, <2>, <4>>>):
  ; // CHECK: [[UA:%[0-9]+]] = "nbdl.unwrap"([[A]])
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NEXT: [[UB:%[0-9]+]] = "nbdl.unwrap"([[B]])
  ; // CHECK-NEXT: func.call @"::foo::test_dot"([[UA]], [[UB]])
  ; // CHECK-NEXT: [[UNIT:%[0-9]+]] = "nbdl.unit"()
  ; // CHECK-NEXT: "nbdl.discard"([[UNIT]])
  (define-match-fn test_call_dot (A B Fn)
    (match-params ((X : !vec3 A)
                   (Y : !vec3 B))
      (visit test_dot X Y)))

  ; // The result types of functions defined with define-fn are inferred
  ; // by the geomalg-expand pass registered by (nbdl spec geomalg).
  ; // CHECK-LABEL: func.func @add_vec3(
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: "geomalg.expand"
  ; // CHECK: "nbdl.return"
  ; // CHECK-SAME: (!geomalg.multivector<<1>, <2>, <4>>) -> ()
  (define-fn add_vec3 ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (sum A B)))

  ; // A !geomalg.unknown value may be returned as a placeholder.
  ; // CHECK-LABEL: func.func @reflect_vec3(
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NOT: "geomalg.vprod"
  ; // CHECK: "nbdl.return"
  ; // CHECK-SAME: (!geomalg.multivector<<1>, <2>, <4>>) -> ()
  (define-fn reflect_vec3 ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (vprod A B)))

  ; // The result types of visits are inferred from the inferred
  ; // result types of the callees.
  ; // CHECK-LABEL: func.func @dot_sums(
  ; // CHECK-SAME: -> !geomalg.blade<0>
  ; // CHECK: [[SUM1:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: [[SUM2:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: [[DOT:%[0-9]+]] = "nbdl.visit"({{%[0-9]+}}, [[SUM1]], [[SUM2]])
  ; // CHECK-SAME: -> !geomalg.blade<0>
  ; // CHECK-NEXT: "nbdl.return"([[DOT]])
  (define-fn dot_sums ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (visit test_dot (visit add_vec3 A B) (visit add_vec3 B A))))

  ; // An expr binds the results of visits for use with geomalg operations.
  ; // The operands are !nbdl.unknown until the visits are inferred.
  ; // CHECK-LABEL: func.func @dot_sum_expr(
  ; // CHECK-SAME: -> !geomalg.blade<0>
  ; // CHECK: [[SUM:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NEXT: [[DOT:%[0-9]+]] = "geomalg.dot"([[SUM]], %arg1)
  ; // CHECK-SAME: (!geomalg.multivector<<1>, <2>, <4>>,
  ; // CHECK-SAME: !geomalg.multivector<<1>, <2>, <4>>) -> !geomalg.blade<0>
  ; // CHECK-NEXT: "nbdl.return"([[DOT]])
  (define-fn dot_sum_expr ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (visit (lambda (S) (dot S B))
                   (visit add_vec3 A B))))

  ; // The result type of a geomalg operation with a !nbdl.unknown
  ; // operand is inferred once the operand type is inferred.
  ; // CHECK-LABEL: func.func @sum_sum_expr(
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK: [[SUM:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NEXT: "geomalg.expand"([[SUM]])
  ; // CHECK: "nbdl.return"
  ; // CHECK-SAME: (!geomalg.multivector<<1>, <2>, <4>>) -> ()
  (define-fn sum_sum_expr ((A : !vec3) (B : !vec3)) -> (!nbdl.unknown)
    (return (visit (lambda (S) (sum S A))
                   (visit add_vec3 A B))))

  ; // A value narrowed by simplification is widened
  ; // to keep the explicit result type of the function.
  ; // CHECK-LABEL: func.func @"::foo::cancel_vec3"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-COUNT-3: "geomalg.blade"() <{coefficient = 0.0
  ; // CHECK-NEXT: [[SUM:%[0-9]+]] = "geomalg.sum"
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NEXT: "nbdl.return"([[SUM]])
  (define-fn cancel_vec3 ((A : !vec3)) -> (!vec3)
    (return (sum A (negate A))))

  (finalize-module)

  (write-nbdl-module)

#| ; // FIXME c++ preprocessor directives unavailable here
; // TODO Check incorrect type mapping.
#ifdef TEST_INVALID_PARAMS
  // expected-error@+4 {{invalid visit argument #2 or something}}
  (define-match-fn test_test_dot_fail (Store Fn)
    (match-params ((A : 'nbdl::vec_f32<3> (get Store '.a))
                   (B : 'nbdl::vec_f32<2> (get Store '.b)))
      (visit test_dot A B)))
#endif
|#

}
} // namespace foo
} // namespace

struct vec3_pair {
  geomalg::vec3 a;
  geomalg::vec3 b;
};

bool vec_equal(geomalg::vec3 A, geomalg::vec3 B) {
  return __builtin_reduce_and(A.value == B.value);
}

int main() {
  // Call the lowered geomalg functions directly.
  SCHIR_ASSERT(foo::test_dot({{3, 2, 4}}, {{4, 2, 1}}).value == 20);
  SCHIR_ASSERT(foo::test_dot({{1, 0, 0}}, {{0, 1, 0}}).value == 0);
  SCHIR_ASSERT(foo::test_dot({{-1, 2, 0.5}}, {{2, 3, 4}}).value == 6);
  SCHIR_ASSERT(vec_equal(foo::test_add({{3, 2, 4}}, {{4, 2, 1}}), {{7, 4, 5}}));
  SCHIR_ASSERT(vec_equal(foo::test_add({{1, 0, 0}}, {{0, 1, 0}}), {{1, 1, 0}}));
  SCHIR_ASSERT(vec_equal(foo::test_add({{-1, 2, 0.5}}, {{1, -2, -0.5}}),
                         {{0, 0, 0}}));

  SCHIR_ASSERT(vec_equal(foo::cancel_vec3({{3, 2, 4}}), {{0, 0, 0}}));

  vec3_pair Store{{{3, 2, 4}}, {{4, 2, 1}}};

  float Result = 0;
  foo::test_test_dot(Store, [&](geomalg::scalar S) { Result = S.value; });
  SCHIR_ASSERT(Result == 20);

  // The result is discarded.
  foo::test_test_call(Store, [](auto&&) { });
}
