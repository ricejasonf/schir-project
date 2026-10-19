// RUN: clang++ -std=c++26 -I %schir_module_path -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <schir/SCHIR_ASSERT.h>

template <unsigned n>
using vec = float __attribute__((ext_vector_type(n)));
using vec2 = vec<2>;
using vec3 = vec<3>;

template <unsigned n>
constexpr inline bool vec_near(vec<n> a, vec<n> b, float Tol = 1e-5f) {
  vec<n> d = a - b;
  return __builtin_reduce_max(d * d) <= Tol * Tol;
}

constexpr float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

extern "C" vec3 up_down(vec3);
extern "C" vec3 reflect_plane(vec3, vec3, float);
extern "C" vec3 reflect_bisector(vec3, vec3, vec3);
extern "C" vec3 invert_sphere(vec3, vec3, float);
extern "C" vec3 invert_unit_sphere(vec3, vec3);
extern "C" vec3 reflect_point(vec3, vec3);
extern "C" vec3 reflect_line(vec3, vec3, vec3);
extern "C" vec3 translate(vec3, vec3);
extern "C" vec3 translate_dist(vec3, vec3, float);
extern "C" vec3 rotate(vec3, vec3, vec3);
extern "C" vec3 rotate_about(vec3, vec3, vec3, vec3);
extern "C" vec3 dilate(vec3, vec3, float);
extern "C" vec3 transverse(vec3, vec3);
extern "C" vec3 screw(vec3, vec3, vec3, vec3);
extern "C" vec3 screw_undo(vec3, vec3, vec3, vec3);
extern "C" vec2 reflect_line_2d(vec2, vec2, vec2);
extern "C" vec2 reflect_point_2d(vec2, vec2);
extern "C" vec2 motor_2d(vec2, vec2, vec2, vec2, vec2);
extern "C" vec2 dilate_2d(vec2, vec2, float);

#pragma schir_scheme
{
(import (schir base)
        (schir llvm pass)
        (schir mlir)
        (schir mlir all-passes)
        (geomalg base)
        (geomalg versor)
        (geomalg conformal))

(with-metric 'cga)

(define-func up_down ((x : !vec3))
  (convert (down (up x)) !vec3))

(define-func reflect_plane ((x : !vec3) (n : !vec3) (d : !scalar))
  (convert (down (vprod (up x) (plane n d))) !vec3))

(define-func reflect_bisector ((x : !vec3) (a : !vec3) (b : !vec3))
  (convert (down (vprod (up x) (bisector a b))) !vec3))

(define-func invert_sphere ((x : !vec3) (c : !vec3) (r : !scalar))
  (convert (down (vprod (up x) (sphere c r))) !vec3))

(define-func invert_unit_sphere ((x : !vec3) (c : !vec3))
  (convert (down (vprod (up x) (sphere c 1))) !vec3))

(define-func reflect_point ((x : !vec3) (c : !vec3))
  (convert (down (vprod (up x) (point c))) !vec3))

(define-func reflect_line ((x : !vec3) (c : !vec3) (u : !vec3))
  (convert (down (vprod (up x) (line c u))) !vec3))

(define-func translate ((x : !vec3) (t : !vec3))
  (convert (down (vprod (up x) (translator t))) !vec3))

(define-func translate_dist ((x : !vec3) (n : !vec3) (d : !scalar))
  (convert (down (vprod (up x) (translator n d))) !vec3))

(define-func rotate ((x : !vec3) (a : !vec3) (b : !vec3))
  (convert (down (vprod (up x) (rotor a b))) !vec3))

(define-func rotate_about ((x : !vec3) (a : !vec3) (b : !vec3) (c : !vec3))
  (convert (down (vprod (up x) (rotor a b c))) !vec3))

(define-func dilate ((x : !vec3) (c : !vec3) (s : !scalar))
  (convert (down (vprod (up x) (dilator c s))) !vec3))

(define-func transverse ((x : !vec3) (b : !vec3))
  (convert (down (vprod (up x) (transversor b))) !vec3))

; // Rotate then translate along the rotation axis.
(define-func screw ((x : !vec3) (a : !vec3) (b : !vec3) (t : !vec3))
  (convert (down (vprod (up x) (versor (translator t) (rotor a b)))) !vec3))

(define-func screw_undo ((x : !vec3) (a : !vec3) (b : !vec3) (t : !vec3))
  (define M (versor (translator t) (rotor a b)))
  (convert (down (vprod (vprod (up x) M) (inverse M))) !vec3))

(define-func reflect_line_2d ((x : !vec2) (c : !vec2) (u : !vec2))
  (convert (down (vprod (up x) (line c u))) !vec2))

(define-func reflect_point_2d ((x : !vec2) (c : !vec2))
  (convert (down (vprod (up x) (point c))) !vec2))

; // Rotate about c then translate.
(define-func motor_2d ((x : !vec2) (a : !vec2) (b : !vec2) (c : !vec2)
                       (t : !vec2))
  (convert (down (vprod (up x) (versor (translator t) (rotor a b c)))) !vec2))

(define-func dilate_2d ((x : !vec2) (c : !vec2) (s : !scalar))
  (convert (down (vprod (up x) (dilator c s))) !vec2))

(run-passes geomalg-current-module
            "geomalg-to-llvm")
(inject-module geomalg-current-module)
}

int main() {
  constexpr vec3 X = vec3{1, 2, 4};
  constexpr vec3 C = vec3{1, 1, 2};
  constexpr vec3 E1 = vec3{1, 0, 0};
  constexpr vec3 E2 = vec3{0, 1, 0};
  constexpr vec3 E3 = vec3{0, 0, 1};
  constexpr vec3 E12 = vec3{1, 1, 0};

  SCHIR_ASSERT(vec_near(up_down(X), X));

  // Planes n·x = d need not have a unit normal.
  SCHIR_ASSERT(vec_near(reflect_plane(X, E3, 1), vec3{1, 2, -2}));
  SCHIR_ASSERT(vec_near(reflect_plane(X, 2 * E3, 2), vec3{1, 2, -2}));
  SCHIR_ASSERT(vec_near(reflect_plane(X, E12, 0), vec3{-2, -1, 4}));

  constexpr vec3 P = vec3{1, 2, 3};
  constexpr vec3 Q = vec3{-2, 0.5, 1};
  SCHIR_ASSERT(vec_near(reflect_bisector(P, P, Q), Q));
  SCHIR_ASSERT(vec_near(reflect_bisector(Q, P, Q), P));

  // x ↦ c + ρ²(x - c)/|x - c|²
  constexpr vec3 D = X - C;
  SCHIR_ASSERT(vec_near(invert_sphere(X, C, 2), C + 4 * D / dot(D, D)));
  SCHIR_ASSERT(vec_near(invert_unit_sphere(X, C), C + D / dot(D, D)));
  SCHIR_ASSERT(vec_near(invert_sphere(C + 2 * E2, C, 2), C + 2 * E2));

  SCHIR_ASSERT(vec_near(reflect_point(X, C), 2 * C - X));

  // A half-turn about the line through C along E2 + E3.
  constexpr vec3 U = vec3{0, 1, 1};
  SCHIR_ASSERT(vec_near(reflect_line(C + 2 * U, C, U), C + 2 * U));
  SCHIR_ASSERT(vec_near(reflect_line(C + E1, C, U), C - E1));
  SCHIR_ASSERT(vec_near(reflect_line(C + U + E1, C, U), C + U - E1));
  SCHIR_ASSERT(vec_near(reflect_line(C + E2, C, U), C + E3));

  SCHIR_ASSERT(vec_near(translate(X, vec3{3, -1, 2}), X + vec3{3, -1, 2}));
  SCHIR_ASSERT(vec_near(translate_dist(X, E3, 3), X + 3 * E3));
  SCHIR_ASSERT(vec_near(translate_dist(X, E3, 0), X));

  // Rotate by twice the angle from a to b.
  SCHIR_ASSERT(vec_near(rotate(E1, E1, E12), E2));
  SCHIR_ASSERT(vec_near(rotate(X, E1, E12), vec3{-2, 1, 4}));
  SCHIR_ASSERT(vec_near(rotate(X, E1, 2 * E1), X));
  SCHIR_ASSERT(vec_near(rotate_about(C + E1, E1, E12, C), C + E2));
  SCHIR_ASSERT(vec_near(rotate_about(C + E3, E1, E12, C), C + E3));

  SCHIR_ASSERT(vec_near(dilate(X, vec3{0, 0, 0}, 2.5), 2.5f * X));
  SCHIR_ASSERT(vec_near(dilate(X, C, 2.5), C + 2.5f * D, 1e-3));
  SCHIR_ASSERT(vec_near(dilate(X, C, 1), X));
  SCHIR_ASSERT(vec_near(dilate(X, C, -1), 2 * C - X));

  // x ↦ (x + x²b) / (1 + 2b·x + b²x²)
  constexpr vec3 B = vec3{0.5, -0.25, 1};
  SCHIR_ASSERT(vec_near(transverse(X, B),
                        (X + dot(X, X) * B) /
                          (1 + 2 * dot(B, X) + dot(B, B) * dot(X, X))));

  SCHIR_ASSERT(vec_near(screw(E1, E1, E12, 5 * E3), E2 + 5 * E3));
  SCHIR_ASSERT(vec_near(screw_undo(X, E1, E12, vec3{1, 2, 3}), X));

  constexpr vec2 Y = vec2{3, 1};
  constexpr vec2 C2 = vec2{1, 1};
  SCHIR_ASSERT(vec_near(reflect_line_2d(Y, C2, vec2{1, 1}), vec2{1, 3}));
  SCHIR_ASSERT(vec_near(reflect_point_2d(Y, C2), vec2{-1, 1}));
  SCHIR_ASSERT(vec_near(motor_2d(Y, vec2{1, 0}, vec2{1, 1}, C2, vec2{0, -2}),
                        vec2{1, 1}));
  SCHIR_ASSERT(vec_near(dilate_2d(Y, C2, 3), vec2{7, 1}));
}
