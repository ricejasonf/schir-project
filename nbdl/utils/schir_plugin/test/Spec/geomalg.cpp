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

  ;(export-c test_dot)

  (define-geomalg-fn test_dot ((Point : !vec3) (Axis : !uvec3))
    (dot Point Axis))

  (match-params-fn test_test_dot (Store Fn)
    (match (get Store '.point)
      (!vec3 =>
        (lambda (P)
          (match (get Store '.axis)
            (!uvec3 =>
              (lambda (A)
                (visit Fn (visit test_dot P A)))))))))

#| /* TODO A syntax like let would be nice.
    (match-params ((P (get Store '.point))
                   (A (get Store '.axis)))
      (visit test_dot (convert P !vec3)
                      (convert A !uvec3))))
   */ |#

#| /* TODO Finish... something
  (define-geomalg-fn test_test_test_dot ((Points : !memref<?x!vec3>)
    (match-each Points
      (lambda (Point)
    */ |#





    ;(visit test_dot)

}
} // namespace foo
} // namespace

int main() {
  // TODO run test_test_dot
}
