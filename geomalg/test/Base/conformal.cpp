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
        (geomalg versor)
        (geomalg conformal-3d)
        (rename (only (geomalg conformal-2d) point line negative-dilator)
                (point point-2d)
                (line line-2d)
                (negative-dilator negative-dilator-2d))
        (only (schir base) define-values))

(export-c up_down reflect_plane reflect_bisector invert_sphere
          invert_unit_sphere reflect_point reflect_line translate
          translate_dist rotate rotate_about dilate transverse screw screw_undo
          reflect_line_2d reflect_point_2d motor_2d dilate_2d
          reflect_plane_point reflect_line_point invert_sphere_point
          reflect_line_point_2d reflect_plane_weight reflect_point_weight
          reflect_line_weight reflect_point_weight_2d dilate_weight
          negative_dilate negative_dilate_const negative_dilate_2d
          negative_dilate_weight negative_dilate_weight_2d)

; // Get the Euclidean part of a conformal point in 3D.
(define (euclid X)
  (define-values (x1 x2 x3 xo xi)
    (expand (convert X (!multivector !e1 !e2 !e3 !no !ni))))
  (sum x1 x2 x3))

; // Get the Euclidean part of a conformal point in 2D.
(define (euclid-2d X)
  (define-values (x1 x2 xo xi)
    (expand (convert X (!multivector !e1 !e2 !no !ni))))
  (sum x1 x2))

(define-fn up_down ((x : !vec3)) -> (!vec3)
  (return (convert (down (up x)) !vec3)))

(define-fn reflect_plane ((x : !vec3) (n : !vec3) (d : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (plane n d))) !vec3)))

(define-fn reflect_bisector ((x : !vec3) (a : !vec3) (b : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (bisector a b))) !vec3)))

(define-fn invert_sphere ((x : !vec3) (c : !vec3) (r : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (sphere c r))) !vec3)))

(define-fn invert_unit_sphere ((x : !vec3) (c : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (sphere c 1))) !vec3)))

(define-fn reflect_point ((x : !vec3) (c : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (point c))) !vec3)))

(define-fn reflect_line ((x : !vec3) (c : !vec3) (u : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (line c u))) !vec3)))

(define-fn translate ((x : !vec3) (t : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (translator t))) !vec3)))

(define-fn translate_dist ((x : !vec3) (n : !vec3) (d : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (translator n d))) !vec3)))

(define-fn rotate ((x : !vec3) (a : !vec3) (b : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (rotor a b))) !vec3)))

(define-fn rotate_about ((x : !vec3) (a : !vec3) (b : !vec3) (c : !vec3))
                         -> (!vec3)
  (return (convert (down (vprod (up x) (rotor a b c))) !vec3)))

(define-fn dilate ((x : !vec3) (c : !vec3) (s : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (dilator c s))) !vec3)))

(define-fn transverse ((x : !vec3) (b : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (transversor b))) !vec3)))

; // Rotate then translate along the rotation axis.
(define-fn screw ((x : !vec3) (a : !vec3) (b : !vec3) (t : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (versor (translator t) (rotor a b))))
                   !vec3)))

(define-fn screw_undo ((x : !vec3) (a : !vec3) (b : !vec3) (t : !vec3))
                       -> (!vec3)
  (define M (versor (translator t) (rotor a b)))
  (return (convert (down (vprod (vprod (up x) M) (inverse M))) !vec3)))

(define-fn reflect_line_2d ((x : !vec2) (c : !vec2) (u : !vec2)) -> (!vec2)
  (return (convert (down (vprod (up x) (line-2d c u))) !vec2)))

(define-fn reflect_point_2d ((x : !vec2) (c : !vec2)) -> (!vec2)
  (return (convert (down (vprod (up x) (point-2d c))) !vec2)))

; // Rotate about c then translate.
(define-fn motor_2d ((x : !vec2) (a : !vec2) (b : !vec2) (c : !vec2)
                      (t : !vec2)) -> (!vec2)
  (return (convert (down (vprod (up x) (versor (translator t) (rotor a b c))))
                   !vec2)))

(define-fn dilate_2d ((x : !vec2) (c : !vec2) (s : !scalar)) -> (!vec2)
  (return (convert (down (vprod (up x) (dilator c s))) !vec2)))

; // Planes map normalized points to normalized points.
(define-fn reflect_plane_point ((x : !vec3) (n : !vec3) (d : !scalar))
                                -> (!vec3)
  (return (euclid (vprod (up x) (plane n d)))))

(define-fn reflect_line_point ((x : !vec3) (c : !vec3) (u : !vec3)) -> (!vec3)
  (return (euclid (vprod (up x) (line c u)))))

(define-fn reflect_line_point_2d ((x : !vec2) (c : !vec2) (u : !vec2))
                                  -> (!vec2)
  (return (euclid-2d (vprod (up x) (line-2d c u)))))

(define-fn invert_sphere_point ((x : !vec3) (c : !vec3) (r : !scalar))
                                -> (!vec3)
  (return (euclid (normalize-point (vprod (up x) (sphere c r))))))

(define-fn reflect_plane_weight ((x : !vec3) (n : !vec3) (d : !scalar))
                                 -> (!scalar)
  (return (point-weight (vprod (up x) (plane n d)))))

(define-fn reflect_point_weight ((x : !vec3) (c : !vec3)) -> (!scalar)
  (return (point-weight (vprod (up x) (point c)))))

(define-fn reflect_line_weight ((x : !vec3) (c : !vec3) (u : !vec3))
                                -> (!scalar)
  (return (point-weight (vprod (up x) (line c u)))))

(define-fn reflect_point_weight_2d ((x : !vec2) (c : !vec2)) -> (!scalar)
  (return (point-weight (vprod (up x) (point-2d c)))))

; // Dilators with s < 0 negate the weight of a point.
(define-fn dilate_weight ((x : !vec3) (c : !vec3) (s : !scalar)) -> (!scalar)
  (return (point-weight (vprod (up x) (dilator c s)))))

(define-fn negative_dilate ((x : !vec3) (c : !vec3) (s : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (negative-dilator c s))) !vec3)))

(define-fn negative_dilate_const ((x : !vec3) (c : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (negative-dilator c -2))) !vec3)))

(define-fn negative_dilate_2d ((x : !vec2) (c : !vec2) (s : !scalar))
                               -> (!vec2)
  (return (convert (down (vprod (up x) (negative-dilator-2d c s))) !vec2)))

; // Negative dilators keep the sign of the weight of a point.
(define-fn negative_dilate_weight ((x : !vec3) (c : !vec3) (s : !scalar))
                                   -> (!scalar)
  (return (point-weight (vprod (up x) (negative-dilator c s)))))

(define-fn negative_dilate_weight_2d ((x : !vec2) (c : !vec2) (s : !scalar))
                                      -> (!scalar)
  (return (point-weight (vprod (up x) (negative-dilator-2d c s)))))

(finalize-module)
}
} // namespace test
} // namespace

using namespace test;
using geomalg::near;
using geomalg::vec2;
using geomalg::vec3;

constexpr float dot(vec3 a, vec3 b) {
  return a.value.x * b.value.x + a.value.y * b.value.y +
         a.value.z * b.value.z;
}

int main() {
  constexpr vec3 X{1, 2, 4};
  constexpr vec3 C{1, 1, 2};
  constexpr vec3 E1{1, 0, 0};
  constexpr vec3 E2{0, 1, 0};
  constexpr vec3 E3{0, 0, 1};
  constexpr vec3 E12{1, 1, 0};

  SCHIR_ASSERT(near(up_down(X), X));

  // Planes n·x = d need not have a unit normal.
  SCHIR_ASSERT(near(reflect_plane(X, E3, 1), vec3{1, 2, -2}));
  SCHIR_ASSERT(near(reflect_plane(X, 2 * E3, 2), vec3{1, 2, -2}));
  SCHIR_ASSERT(near(reflect_plane(X, E12, 0), vec3{-2, -1, 4}));

  constexpr vec3 P{1, 2, 3};
  constexpr vec3 Q{-2, 0.5, 1};
  SCHIR_ASSERT(near(reflect_bisector(P, P, Q), Q));
  SCHIR_ASSERT(near(reflect_bisector(Q, P, Q), P));

  // x ↦ c + ρ²(x - c)/|x - c|²
  constexpr vec3 D = X - C;
  SCHIR_ASSERT(near(invert_sphere(X, C, 2), C + 4 * D / dot(D, D)));
  SCHIR_ASSERT(near(invert_unit_sphere(X, C), C + D / dot(D, D)));
  SCHIR_ASSERT(near(invert_sphere(C + 2 * E2, C, 2), C + 2 * E2));

  SCHIR_ASSERT(near(reflect_point(X, C), 2 * C - X));

  // A half-turn about the line through C along E2 + E3.
  constexpr vec3 U{0, 1, 1};
  SCHIR_ASSERT(near(reflect_line(C + 2 * U, C, U), C + 2 * U));
  SCHIR_ASSERT(near(reflect_line(C + E1, C, U), C - E1));
  SCHIR_ASSERT(near(reflect_line(C + U + E1, C, U), C + U - E1));
  SCHIR_ASSERT(near(reflect_line(C + E2, C, U), C + E3));

  SCHIR_ASSERT(near(translate(X, vec3{3, -1, 2}), X + vec3{3, -1, 2}));
  SCHIR_ASSERT(near(translate(X, vec3{0, 0, 0}), X));
  SCHIR_ASSERT(near(translate_dist(X, E3, 3), X + 3 * E3));
  SCHIR_ASSERT(near(translate_dist(X, E3, 0), X));

  // Rotate by twice the angle from a to b.
  SCHIR_ASSERT(near(rotate(E1, E1, E12), E2));
  SCHIR_ASSERT(near(rotate(X, E1, E12), vec3{-2, 1, 4}));
  SCHIR_ASSERT(near(rotate(X, E1, 2 * E1), X));
  SCHIR_ASSERT(near(rotate_about(C + E1, E1, E12, C), C + E2));
  SCHIR_ASSERT(near(rotate_about(C + E3, E1, E12, C), C + E3));

  SCHIR_ASSERT(near(dilate(X, vec3{0, 0, 0}, 2.5), 2.5f * X));
  SCHIR_ASSERT(near(dilate(X, C, 2.5), C + 2.5f * D));
  SCHIR_ASSERT(near(dilate(X, C, 1), X));
  SCHIR_ASSERT(near(dilate(X, C, -1), 2 * C - X));
  SCHIR_ASSERT(near(negative_dilate(X, C, -1), 2 * C - X));
  SCHIR_ASSERT(near(negative_dilate(X, C, -2.5), C - 2.5f * D));
  SCHIR_ASSERT(near(negative_dilate_const(X, C), C - 2 * D));

  // Keep the precision of dilations far from the origin.
  constexpr vec3 F{100, -100, 100};
  SCHIR_ASSERT(near(dilate(F + D, F, 2.5), F + 2.5f * D, 1e-3));
  SCHIR_ASSERT(near(negative_dilate(F + D, F, -2.5), F - 2.5f * D, 1e-3));

  // x ↦ (x + x²b) / (1 + 2b·x + b²x²)
  constexpr vec3 B{0.5, -0.25, 1};
  SCHIR_ASSERT(near(transverse(X, B),
                    (X + dot(X, X) * B) /
                      (1 + 2 * dot(B, X) + dot(B, B) * dot(X, X))));

  SCHIR_ASSERT(near(screw(E1, E1, E12, 5 * E3), E2 + 5 * E3));
  SCHIR_ASSERT(near(screw_undo(X, E1, E12, vec3{1, 2, 3}), X));

  constexpr vec2 Y{3, 1};
  constexpr vec2 C2{1, 1};
  SCHIR_ASSERT(near(reflect_line_2d(Y, C2, vec2{1, 1}), vec2{1, 3}));
  SCHIR_ASSERT(near(reflect_point_2d(Y, C2), vec2{-1, 1}));
  SCHIR_ASSERT(near(motor_2d(Y, vec2{1, 0}, vec2{1, 1}, C2, vec2{0, -2}),
                    vec2{1, 1}));
  SCHIR_ASSERT(near(dilate_2d(Y, C2, 3), vec2{7, 1}));
  SCHIR_ASSERT(near(negative_dilate_2d(Y, C2, -1), vec2{-1, 1}));
  SCHIR_ASSERT(near(negative_dilate_2d(Y, C2, -3), vec2{-5, 1}));
  SCHIR_ASSERT(near(dilate_2d(vec2{102, -100}, vec2{100, -100}, 3),
                    vec2{106, -100}, 1e-3));

  SCHIR_ASSERT(near(reflect_plane_point(X, E3, 1), vec3{1, 2, -2}));
  SCHIR_ASSERT(near(reflect_plane_point(X, 2 * E3, 2), vec3{1, 2, -2}));
  SCHIR_ASSERT(near(reflect_plane_point(X, E12, 0), vec3{-2, -1, 4}));
  SCHIR_ASSERT(near(reflect_line_point(C + E1, C, U), C - E1));
  SCHIR_ASSERT(near(reflect_line_point(C + E2, C, U), C + E3));
  SCHIR_ASSERT(near(reflect_line_point_2d(Y, C2, vec2{1, 1}), vec2{1, 3}));
  SCHIR_ASSERT(near(invert_sphere_point(X, C, 2), C + 4 * D / dot(D, D)));

  SCHIR_ASSERT(near(reflect_plane_weight(X, E3, 1), 1));
  SCHIR_ASSERT(near(reflect_plane_weight(X, 2 * E12, 3), 1));
  SCHIR_ASSERT(near(reflect_point_weight(X, C), 1));
  SCHIR_ASSERT(near(reflect_line_weight(X, C, U), 1));
  SCHIR_ASSERT(near(reflect_point_weight_2d(Y, C2), 1));
  SCHIR_ASSERT(near(dilate_weight(X, C, -1), -1));
  SCHIR_ASSERT(near(negative_dilate_weight(X, C, -1), 1));
  SCHIR_ASSERT(negative_dilate_weight(X, C, -2.5).value > 0);
  SCHIR_ASSERT(near(negative_dilate_weight_2d(Y, C2, -1), 1));
  SCHIR_ASSERT(negative_dilate_weight_2d(Y, C2, -3).value > 0);
}
