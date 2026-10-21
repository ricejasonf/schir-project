// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <geomalg/nbdl.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace {
namespace test {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec geomalg)
        (geomalg base)
        (only (schir base) define-values))

(export-c test_dot test_reflect_1)

(define-fn test_dot ((point : !vec3) (axis : !uvec3)) -> (!nbdl.unknown)
  (return (dot point axis)))

; // Reflect a point over the axis created by a unit vector.
; // Let p be the euclidean part of a 3d point.
; // Use ReflAxis to reflect on an axis through the origin.
; // Bind the result of visiting test_dot for use with geomalg operations.
(define-fn test_reflect_1 ((p : !vec3) (ReflAxis : !uvec3)) -> (!nbdl.unknown)
  (return
    (visit
      (lambda (p_dot_p)
        (define point
          (sum
            p
            (no 1)
            (iprod (iprod (scalar .5) p_dot_p)
                   (ni 1))))
        (define point2
          (vprod point ReflAxis))
        (define-values (v1 v2 v3 vo vi)
          (expand (convert point2
                    (!multivector !e1 !e2 !e3 !no !ni))))
        (sum v1 v2 v3))
      (visit test_dot p p))))

(finalize-module)
}
} // namespace test
} // namespace

using namespace test;
using geomalg::near;
using geomalg::vec3;

int main() {
  SCHIR_ASSERT(test_dot(vec3{3, 2, 4}, vec3{4, 2, 1}).value == 20);
  SCHIR_ASSERT(near(test_reflect_1(vec3{1.0, 2, 4}, vec3{0, 1, 0}),
                    vec3{-1, 2, -4}, 0));
  SCHIR_ASSERT(near(test_reflect_1(vec3{1.0, 2, 4}, vec3{0, 0, 1}),
                    vec3{-1, -2, 4}, 0));
  SCHIR_ASSERT(near(test_reflect_1(vec3{1, 0, 0},
                                   vec3{.707106, .707106, 0}),
                    vec3{0, 1, 0}, 0));
  constexpr float PiDiv4 = .707106;
  constexpr vec3 Axis45 = vec3{PiDiv4, PiDiv4, 0};
  SCHIR_ASSERT(near(test_reflect_1(vec3{-1, 0, 0}, Axis45),
                    vec3{0, -1, 0}, 0));
  SCHIR_ASSERT(near(test_reflect_1(vec3{0, 1, 0}, Axis45),
                    vec3{1, 0, 0}, 0));
  SCHIR_ASSERT(near(test_reflect_1(test_reflect_1(vec3{0, 1, 0}, Axis45),
                                   Axis45),
                    vec3{0, 1, 0}, 0));
}
