// RUN: clang++ -std=c++26 -I %schir_module_path -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <schir/SCHIR_ASSERT.h>

// Note that these are not strong types.
template <unsigned n>
using vec = float __attribute__((ext_vector_type(n)));
using vec2 = vec<2>;
using vec3 = vec<3>;

template <unsigned n>
constexpr inline bool vec_equal(vec<n> a, vec<n> b) {
  return __builtin_reduce_and(a == b);
}

extern "C" float test_dot(vec3, vec3);
extern "C" vec3 test_reflect_1(vec3, vec3);

#pragma schir_scheme
{
(import (schir base)
        (schir llvm pass)
        (schir mlir all-passes)
        (geomalg base))

; // Reflect a point over the axis created by a unit vector.
(define-func test_dot ((point : !vec3) (axis : !uvec3))
  (dot point axis))

; // Let p be the euclidean part of a 3d point.
; // Use ReflAxis to reflect on an axis through the origin.
(define-func test_reflect_1 ((p : !vec3) (ReflAxis : !uvec3))
  (define point
    (sum
      p
      (no 1)
      (iprod (iprod (scalar .5)
                 (dot p p))
             (ni 1))))
  (define point2
    (vprod point ReflAxis))
  (define-values (v1 v2 v3 vo vi)
    (expand (convert point2
              (!multivector !e1 !e2 !e3 !no !ni))))
  (sum v1 v2 v3))

(run-passes geomalg-current-module 
          "geomalg-to-llvm")
(inject-module geomalg-current-module)
}

int main() {
  SCHIR_ASSERT(test_dot(vec3{3, 2, 4}, vec3{4, 2, 1}) == 20);
  SCHIR_ASSERT(vec_equal(test_reflect_1(vec3{1.0, 2, 4}, vec3{0, 1, 0}),
                         vec3{-1, 2, -4}));
  SCHIR_ASSERT(vec_equal(test_reflect_1(vec3{1.0, 2, 4}, vec3{0, 0, 1}),
                         vec3{-1, -2, 4}));
  SCHIR_ASSERT(vec_equal(test_reflect_1(vec3{1, 0, 0},
                                        vec3{.707106, .707106, 0}),
                         vec3{0, 1, 0}));
  constexpr float PiDiv4 = .707106;
  constexpr vec3 Axis45 = vec3{PiDiv4, PiDiv4, 0};
  SCHIR_ASSERT(vec_equal(test_reflect_1(vec3{-1, 0, 0}, Axis45),
                         vec3{0, -1, 0}));
  SCHIR_ASSERT(vec_equal(test_reflect_1(vec3{0, 1, 0}, Axis45),
                         vec3{1, 0, 0}));
  SCHIR_ASSERT(vec_equal(test_reflect_1(
                           test_reflect_1(vec3{0, 1, 0}, Axis45),
                           Axis45),
                         vec3{0, 1, 0}));
}
