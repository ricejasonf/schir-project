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

  (export-c test_dot)
  (export-cpp test_test_dot)

  ; // TODO Check the return type (via FileCheck)
  ; //      (ie The define-geomalg-fn should
  ; //       have expand pass run on it.)
  (define-geomalg-fn test_dot ((A : !vec3) (B : !vec3))
    (dot A B))

  ; // CHECK-LABEL: @"::foo::test_test_dot"
  ; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @test_dot}>
  ; // CHECK: "nbdl.visit"([[FN]], %arg{{[0-9]+}}, %arg{{[0-9]+}}) <{validCppCrossMap}>
  (define-match-fn test_test_dot (Store Fn)
    (match-params ((A : 'geomalg::vec3 (get Store '.a))
                   (B : 'geomalg::vec3 (get Store '.b)))
      (visit test_dot A B)))

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

int main() {
  // TODO run test_test_dot
}
