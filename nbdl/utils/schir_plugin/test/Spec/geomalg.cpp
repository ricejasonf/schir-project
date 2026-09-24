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

// TODO It would be nice to generate these declarations.
//      This would require a map from mlir to cpp types.
static_assert(sizeof(geomalg::scalar) == sizeof(float));
static_assert(sizeof(geomalg::vec3) == sizeof(nbdl::vec_f32<3>));
extern "C" geomalg::scalar test_dot(geomalg::vec3, geomalg::vec3);
extern "C" geomalg::vec3 test_add(geomalg::vec3, geomalg::vec3);

namespace {
namespace foo {

#pragma schir_scheme
{
  (import (nbdl spec)
          (nbdl spec geomalg)
          (geomalg base))

  (export-c test_dot test_add)
  (export-cpp test_test_dot test_test_call)

  ; // TODO Check the return type (via FileCheck)
  ; //      (ie The define-geomalg-fn should
  ; //       have expand pass run on it.)
  (define-geomalg-fn test_dot ((A : !vec3) (B : !vec3))
    (dot A B))

  (define-geomalg-fn test_add ((A : !vec3) (B : !vec3))
    (sum A B))

  ; // CHECK-LABEL: @"::foo::test_test_dot"
  ; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @test_dot}>
  ; // CHECK: "nbdl.visit"([[FN]],
  ; // CHECK-SAME: <{validCppCrossMap}>
  (define-match-fn test_test_dot (Store Fn)
    (match-params ((A : 'geomalg::vec3 (get Store '.a))
                   (B : 'geomalg::vec3 (get Store '.b)))
      (visit Fn (visit test_dot A B))))

  ; // Visit test_dot with MLIR typed results lowering to func.call.
  ; // CHECK-LABEL: @"::foo::test_test_call"
  ; // CHECK: [[SUM1:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: <{validCppCrossMap}>
  ; // CHECK: [[SUM2:%[0-9]+]] = "nbdl.visit"
  ; // CHECK-SAME: <{validCppCrossMap}>
  ; // CHECK: [[UA:%[0-9]+]] = "nbdl.unwrap"([[SUM1]])
  ; // CHECK-NEXT: [[UB:%[0-9]+]] = "nbdl.unwrap"([[SUM2]])
  ; // CHECK-NEXT: func.call @test_dot([[UA]], [[UB]])
  (define-match-fn test_test_call (Store Fn)
    (match-params ((A : 'geomalg::vec3 (get Store '.a))
                   (B : 'geomalg::vec3 (get Store '.b)))
      (visit test_dot (visit test_add A B) (visit test_add B A))))

  ; // CHECK-LABEL: @"::foo::test_call_dot"
  ; // CHECK: ^bb0([[A:%arg[0-9]+]]: !nbdl.store<!geomalg.multivector<<1>, <2>, <4>>>):
  ; // CHECK: ^bb0([[B:%arg[0-9]+]]: !nbdl.store<!geomalg.multivector<<1>, <2>, <4>>>):
  ; // CHECK: [[UA:%[0-9]+]] = "nbdl.unwrap"([[A]])
  ; // CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
  ; // CHECK-NEXT: [[UB:%[0-9]+]] = "nbdl.unwrap"([[B]])
  ; // CHECK-NEXT: func.call @test_dot([[UA]], [[UB]])
  ; // CHECK-NEXT: [[UNIT:%[0-9]+]] = "nbdl.unit"()
  ; // CHECK-NEXT: "nbdl.discard"([[UNIT]])
  (define-match-fn test_call_dot (A B Fn)
    (match-params ((X : !vec3 A)
                   (Y : !vec3 B))
      (visit test_dot X Y)))

  (write-nbdl-module)

  (build-geomalg-exports)

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
  // Call the injected geomalg functions directly.
  SCHIR_ASSERT(test_dot({{3, 2, 4}}, {{4, 2, 1}}).value == 20);
  SCHIR_ASSERT(test_dot({{1, 0, 0}}, {{0, 1, 0}}).value == 0);
  SCHIR_ASSERT(test_dot({{-1, 2, 0.5}}, {{2, 3, 4}}).value == 6);
  SCHIR_ASSERT(vec_equal(test_add({{3, 2, 4}}, {{4, 2, 1}}), {{7, 4, 5}}));
  SCHIR_ASSERT(vec_equal(test_add({{1, 0, 0}}, {{0, 1, 0}}), {{1, 1, 0}}));
  SCHIR_ASSERT(vec_equal(test_add({{-1, 2, 0.5}}, {{1, -2, -0.5}}),
                         {{0, 0, 0}}));

  vec3_pair Store{{{3, 2, 4}}, {{4, 2, 1}}};

  float Result = 0;
  foo::test_test_dot(Store, [&](geomalg::scalar S) { Result = S.value; });
  SCHIR_ASSERT(Result == 20);

  // The result is discarded.
  foo::test_test_call(Store, [](auto&&) { });
}
