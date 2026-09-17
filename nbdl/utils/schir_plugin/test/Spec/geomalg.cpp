// RUN: clang++ -std=c++26 \
// RUN:   -I %schir_module_path \
// RUN:   -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

// Copyright Jason Rice 2026

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

  ; // TODO check the return type
  ; //      (ie The define-geomalg-fn should
  ; //       have expand pass run on it.)
  ; // TODO Check incorrect type mapping.
  (define-geomalg-fn test_dot ((A : !vec3) (B : !vec3))
    (dot A B))

  (define-match-fn test_test_dot (Store Fn)
    (match-params ((A : 'nbdl::vec_f32<3> (get Store '.a))
                   (B : 'nbdl::vec_f32<3> (get Store '.b)))
      (visit test_dot A B)))

#|
  (export-c sum_op)
  (export-cpp test_sum)
  ; // TODO This should be in...
  (define (geomalg.sum A B)
    (sum A B))

  (define-match-fn foo_test_sum ((Sum : !vec3)
                                 (Vecs : !memref<?x!vec3>)
                                 Fn)
    (match-each (Vecs)
      (lambda (V)
        (assign Sum (visit geomalg.sum Sum V))))
    (Fn Sum))

  ; // Visiting the non-exported foo_test_sum
  ; // should generate an anonymous function.
  ; // We would need to deduce whether it was compiled
  ; // or generated c++.
  (define-match-fn test_sum (Sum Vecs Fn)
    (visit foo_test_sum Sum Vecs Fn))
    |#

}
} // namespace foo
} // namespace

int main() {
  // TODO run test_test_dot
}
