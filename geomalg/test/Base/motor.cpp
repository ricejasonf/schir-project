// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <geomalg/nbdl.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <array>
#include <cmath>
#include <numbers>

using geomalg::near;
using geomalg::vec2;
using geomalg::vec3;

inline float dot(vec3 a, vec3 b) {
  return a.value.x * b.value.x + a.value.y * b.value.y +
         a.value.z * b.value.z;
}

inline vec3 cross(vec3 a, vec3 b) {
  auto u = a.value;
  auto v = b.value;
  return vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
              u.x * v.y - u.y * v.x};
}

inline vec3 normalize(vec3 a) { return a / std::sqrt(dot(a, a)); }

// Rotate x by the angle about the axis through c with the direction k.
inline vec3 rotate(vec3 x, vec3 k, float Angle, vec3 c) {
  k = normalize(k);
  vec3 v = x - c;
  float Cos = std::cos(Angle);
  float Sin = std::sin(Angle);
  return c + v * Cos + cross(k, v) * Sin + k * dot(k, v) * (1 - Cos);
}

inline vec2 rotate(vec2 x, float Angle, vec2 c) {
  vec3 r = rotate(vec3{x.value.x, x.value.y, 0}, vec3{0, 0, 1}, Angle,
                  vec3{c.value.x, c.value.y, 0});
  return vec2{r.value.x, r.value.y};
}

constexpr float Pi = std::numbers::pi_v<float>;

namespace {
namespace test {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec geomalg)
        (geomalg base)
        (geomalg versor)
        (geomalg conformal)
        (geomalg motor))

(export-c apply_motor apply_exp_log apply_log_product apply_interp
          apply_interp_3 apply_interp_self apply_exp_rotation
          apply_exp_translation apply_exp_screw log_rotation_e12
          apply_interp_half_turn apply_interp_identity apply_translation_interp
          apply_translation_interp_tenth undo_exp_log apply_motor_2d
          apply_exp_log_2d apply_interp_2d apply_interp_motors_2d
          apply_interp_3_2d apply_nlerp
          apply_normalize apply_interp_half_2d apply_nlerp_2d apply_interp_0
          apply_interp_1 apply_interp_half apply_interp_3_4 apply_interp_1_16
          apply_nlerp_half)

; // Motors M = T R rotate in the a ∧ b plane by twice the angle from a to b
; // about c and then translate by d along the unit vector n.
(define (motor n d a b c)
  (versor (translator n d) (rotor a b c)))

(define-fn apply_motor ((x : !vec3) (n : !vec3) (d : !scalar)
                         (a : !vec3) (b : !vec3) (c : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (motor n d a b c))) !vec3)))

(define-fn apply_exp_log ((x : !vec3) (n : !vec3) (d : !scalar)
                           (a : !vec3) (b : !vec3) (c : !vec3)) -> (!vec3)
  (return (convert (down (vprod (up x) (exp (log (motor n d a b c))))) !vec3)))

; // Apply the motor M₁ and then M₂ as the single motor exp(log(M₂ M₁)).
(define-fn apply_log_product ((x : !vec3)
                               (n1 : !vec3) (d1 : !scalar)
                               (a1 : !vec3) (b1 : !vec3) (c1 : !vec3)
                               (n2 : !vec3) (d2 : !scalar)
                               (a2 : !vec3) (b2 : !vec3) (c2 : !vec3))
                              -> (!vec3)
  (define M (gprod (motor n2 d2 a2 b2 c2) (motor n1 d1 a1 b1 c1)))
  (return (convert (down (vprod (up x) (exp (log M)))) !vec3)))

(define-fn apply_interp ((x : !vec3)
                          (n1 : !vec3) (d1 : !scalar)
                          (a1 : !vec3) (b1 : !vec3) (c1 : !vec3)
                          (n2 : !vec3) (d2 : !scalar)
                          (a2 : !vec3) (b2 : !vec3) (c2 : !vec3)
                          (t : !scalar)) -> (!vec3)
  (define V1 (motor n1 d1 a1 b1 c1))
  (define V2 (motor n2 d2 a2 b2 c2))
  (return (convert (down (vprod (up x) V1 (interp V1 V2 t))) !vec3)))

; // Apply V₁ and then interp(V₁, V₂, t) three times.
(define-fn apply_interp_3 ((x : !vec3)
                            (n1 : !vec3) (d1 : !scalar)
                            (a1 : !vec3) (b1 : !vec3) (c1 : !vec3)
                            (n2 : !vec3) (d2 : !scalar)
                            (a2 : !vec3) (b2 : !vec3) (c2 : !vec3)
                            (t : !scalar)) -> (!vec3)
  (define V1 (motor n1 d1 a1 b1 c1))
  (define V2 (motor n2 d2 a2 b2 c2))
  (define M (interp V1 V2 t))
  (return (convert (down (vprod (up x) V1 M M M)) !vec3)))

(define-fn apply_interp_self ((x : !vec3) (n : !vec3) (d : !scalar)
                               (a : !vec3) (b : !vec3) (c : !vec3)
                               (t : !scalar)) -> (!vec3)
  (define V (motor n d a b c))
  (return (convert (down (vprod (up x) V (interp V V t))) !vec3)))

; // Rotate by θ in the plane of the orthonormal u and v with exp(-½θ u ∧ v).
(define-fn apply_exp_rotation ((x : !vec3) (u : !vec3) (v : !vec3)
                                (theta : !scalar)) -> (!vec3)
  (define B (iprod (iprod (scalar -0.5) theta) (oprod u v)))
  (return (convert (down (vprod (up x) (exp B))) !vec3)))

; // Translate by t with exp(-½ t ∧ ∞).
(define-fn apply_exp_translation ((x : !vec3) (t : !vec3)) -> (!vec3)
  (define B (iprod (scalar -0.5) (oprod t (ni 1))))
  (return (convert (down (vprod (up x) (exp B))) !vec3)))

; // Rotate by θ about e₃ and translate by d along e₃.
(define-fn apply_exp_screw ((x : !vec3) (theta : !scalar) (d : !scalar))
                            -> (!vec3)
  (define B (iprod (scalar -0.5)
                   (sum (iprod theta (oprod (e1 1) (e2 1)))
                        (iprod d (oprod (e3 1) (ni 1))))))
  (return (convert (down (vprod (up x) (exp B))) !vec3)))

; // Get the e₁₂ coefficient of log(b a).
(define-fn log_rotation_e12 ((a : !vec3) (b : !vec3)) -> (!nbdl.unknown)
  (return (iprod (oprod (e2 1) (e1 1)) (log (rotor a b)))))

; // The half-turn e₁ e₂ has no scalar part so its log is (π/2) e₁₂
; // and interpolates through the rotation by -tπ.
(define-fn apply_interp_half_turn ((x : !vec3) (t : !scalar)) -> (!vec3)
  (define I (versor (e1 1) (e1 1)))
  (define R (versor (e1 1) (e2 1)))
  (return (convert (down (vprod (up x) (interp I R t))) !vec3)))

; // Interpolate from the identity to a rotation about c.
(define-fn apply_interp_identity ((x : !vec3) (b : !vec3) (c : !vec3)
                                   (t : !scalar)) -> (!vec3)
  (return (convert (down (vprod (up x) (interp (versor) (rotor (e1 1) b c) t)))
                   !vec3)))

; // Interpolate from no translation to the translation by u.
(define-fn apply_translation_interp ((x : !vec3) (u : !vec3) (t : !scalar))
                                     -> (!vec3)
  (define T0 (translator (iprod (scalar 0) u)))
  (return (convert (down (vprod (up x) T0 (interp T0 (translator u) t)))
                   !vec3)))

; // A literal t that is not a power of two.
(define-fn apply_translation_interp_tenth ((x : !vec3) (u : !vec3)) -> (!vec3)
  (define M (interp (versor) (translator u) (scalar 0.1)))
  (return (convert (down (vprod (up x) M)) !vec3)))

; // Apply the motor exp(log(M)) and its inverse.
(define-fn undo_exp_log ((x : !vec3) (n : !vec3) (d : !scalar)
                          (a : !vec3) (b : !vec3) (c : !vec3)) -> (!vec3)
  (define M (exp (log (motor n d a b c))))
  (return (convert (down (vprod (up x) M (inverse M))) !vec3)))

(define-fn apply_motor_2d ((x : !vec2) (n : !vec2) (d : !scalar)
                            (a : !vec2) (b : !vec2) (c : !vec2)) -> (!vec2)
  (return (convert (down (vprod (up x) (motor n d a b c))) !vec2)))

(define-fn apply_exp_log_2d ((x : !vec2) (n : !vec2) (d : !scalar)
                              (a : !vec2) (b : !vec2) (c : !vec2)) -> (!vec2)
  (return (convert (down (vprod (up x) (exp (log (motor n d a b c))))) !vec2)))

; // Interpolate from a rotation about c₁ to a motor.
(define-fn apply_interp_2d ((x : !vec2) (b1 : !vec2) (c1 : !vec2)
                             (n2 : !vec2) (d2 : !scalar)
                             (b2 : !vec2) (c2 : !vec2)
                             (t : !scalar)) -> (!vec2)
  (define V1 (rotor (e1 1) b1 c1))
  (define V2 (motor n2 d2 (e1 1) b2 c2))
  (return (convert (down (vprod (up x) V1 (interp V1 V2 t))) !vec2)))

; // Interpolate between two motors with vec2 arguments passed on the stack.
(define-fn apply_interp_motors_2d ((x : !vec2)
                                   (n1 : !vec2) (d1 : !scalar)
                                   (a1 : !vec2) (b1 : !vec2) (c1 : !vec2)
                                   (n2 : !vec2) (d2 : !scalar)
                                   (a2 : !vec2) (b2 : !vec2) (c2 : !vec2)
                                   (t : !scalar)) -> (!vec2)
  (define V1 (motor n1 d1 a1 b1 c1))
  (define V2 (motor n2 d2 a2 b2 c2))
  (return (convert (down (vprod (up x) V1 (interp V1 V2 t))) !vec2)))

(define-fn apply_interp_3_2d ((x : !vec2) (b1 : !vec2) (c1 : !vec2)
                               (n2 : !vec2) (d2 : !scalar)
                               (b2 : !vec2) (c2 : !vec2)
                               (t : !scalar)) -> (!vec2)
  (define V1 (rotor (e1 1) b1 c1))
  (define V2 (motor n2 d2 (e1 1) b2 c2))
  (define M (interp V1 V2 t))
  (return (convert (down (vprod (up x) V1 M M M)) !vec2)))

;; // Interpolate with a constant t which uses square roots when t = k / 2ⁿ.
(define-syntax define-interp-const
  (syntax-rules ()
    ((_ Name Interp T)
     (define-fn Name ((x : !vec3)
                       (n1 : !vec3) (d1 : !scalar)
                       (a1 : !vec3) (b1 : !vec3) (c1 : !vec3)
                       (n2 : !vec3) (d2 : !scalar)
                       (a2 : !vec3) (b2 : !vec3) (c2 : !vec3)) -> (!vec3)
       (define V1 (motor n1 d1 a1 b1 c1))
       (define V2 (motor n2 d2 a2 b2 c2))
       (return (convert (down (vprod (up x) V1 (Interp V1 V2 T))) !vec3))))))

(define-interp-const apply_interp_0 interp 0)
(define-interp-const apply_interp_1 interp 1)
(define-interp-const apply_interp_half interp 0.5)
(define-interp-const apply_interp_3_4 interp 0.75)
(define-interp-const apply_interp_1_16 interp 0.0625)
(define-interp-const apply_nlerp_half nlerp 0.5)

(define-fn apply_nlerp ((x : !vec3)
                         (n1 : !vec3) (d1 : !scalar)
                         (a1 : !vec3) (b1 : !vec3) (c1 : !vec3)
                         (n2 : !vec3) (d2 : !scalar)
                         (a2 : !vec3) (b2 : !vec3) (c2 : !vec3)
                         (t : !scalar)) -> (!vec3)
  (define V1 (motor n1 d1 a1 b1 c1))
  (define V2 (motor n2 d2 a2 b2 c2))
  (return (convert (down (vprod (up x) V1 (nlerp V1 V2 t))) !vec3)))

; // Normalize the motor scaled by s.
(define-fn apply_normalize ((x : !vec3) (s : !scalar)
                             (n : !vec3) (d : !scalar)
                             (a : !vec3) (b : !vec3) (c : !vec3)) -> (!vec3)
  (define M (normalize (iprod s (motor n d a b c))))
  (return (convert (down (vprod (up x) M)) !vec3)))

(define-fn apply_interp_half_2d ((x : !vec2) (b1 : !vec2) (c1 : !vec2)
                                  (n2 : !vec2) (d2 : !scalar)
                                  (b2 : !vec2) (c2 : !vec2)) -> (!vec2)
  (define V1 (rotor (e1 1) b1 c1))
  (define V2 (motor n2 d2 (e1 1) b2 c2))
  (return (convert (down (vprod (up x) V1 (interp V1 V2 0.5))) !vec2)))

(define-fn apply_nlerp_2d ((x : !vec2) (b1 : !vec2) (c1 : !vec2)
                            (n2 : !vec2) (d2 : !scalar)
                            (b2 : !vec2) (c2 : !vec2)
                            (t : !scalar)) -> (!vec2)
  (define V1 (rotor (e1 1) b1 c1))
  (define V2 (motor n2 d2 (e1 1) b2 c2))
  (return (convert (down (vprod (up x) V1 (nlerp V1 V2 t))) !vec2)))

(finalize-module)
}
} // namespace test
} // namespace

using namespace test;

int main() {
  constexpr vec3 O{0, 0, 0};
  constexpr vec3 E1{1, 0, 0};
  constexpr vec3 E2{0, 1, 0};
  constexpr vec3 E3{0, 0, 1};
  constexpr vec3 X{1, 2, 4};
  constexpr vec3 C{1, 1, 2};
  // Rotations by twice the angle from E1 to these (RG is a general one).
  constexpr vec3 R90{1, 1, 0};
  const vec3 R270{std::cos(Pi * 3 / 4), std::sin(Pi * 3 / 4), 0};
  const vec3 RG = vec3{0.5f, 0, std::sqrt(0.75f)} + vec3{0, 0.25f, 0};
  const vec3 N = normalize(vec3{1, -2, 0.5});

  // The identity motor.
  auto Id = [&](auto f, vec3 x, auto ...rest) {
    return f(x, E3, 0, E1, E1, O, rest...);
  };

  // Pure translation, pure rotation, a screw, and a general motor.
  SCHIR_ASSERT(near(apply_motor(X, E3, 5, E1, E1, C), X + 5 * E3));
  SCHIR_ASSERT(near(apply_exp_log(X, E3, 5, E1, E1, C), X + 5 * E3));
  SCHIR_ASSERT(near(apply_exp_log(X, N, -3, E1, E1, O), X - 3 * N));
  SCHIR_ASSERT(near(apply_exp_log(X, E3, 0, E1, R90, O),
                    rotate(X, E3, Pi / 2, O)));
  SCHIR_ASSERT(near(apply_exp_log(X, E3, 0, E1, R90, C),
                    rotate(X, E3, Pi / 2, C)));
  SCHIR_ASSERT(near(apply_exp_log(X, E3, 5, E1, R90, C),
                    rotate(X, E3, Pi / 2, C) + 5 * E3));
  const vec3 K = cross(E1, RG);
  const vec3 UK = normalize(K);
  const float AG = 2 * std::acos(dot(E1, normalize(RG)));
  SCHIR_ASSERT(near(apply_exp_log(X, UK, 2, E1, RG, C),
                    rotate(X, K, AG, C) + 2 * UK));
  SCHIR_ASSERT(near(apply_exp_log(X, N, 2, E1, RG, C),
                    apply_motor(X, N, 2, E1, RG, C)));
  // The log chooses a rotation by at most π which is the same motion.
  SCHIR_ASSERT(near(apply_exp_log(X, N, 2, E1, R270, C),
                    apply_motor(X, N, 2, E1, R270, C)));

  // Compositions of motors.
  SCHIR_ASSERT(near(apply_log_product(X, E3, 1, E1, R90, C,
                                      N, -2, E1, RG, O),
                    apply_motor(apply_motor(X, E3, 1, E1, R90, C),
                                N, -2, E1, RG, O)));
  SCHIR_ASSERT(near(apply_log_product(X, E2, 3, E1, E1, O,
                                      E1, 0, E1, R90, C),
                    rotate(X + 3 * E2, E3, Pi / 2, C)));

  // Interpolate from V₁ (t = 0) to V₂ (t = 1).
  auto Interp = [&](float t) {
    return apply_interp(X, E3, 1, E1, R90, C, N, 2, E1, RG, O, t);
  };
  SCHIR_ASSERT(near(Interp(0), apply_motor(X, E3, 1, E1, R90, C)));
  SCHIR_ASSERT(near(Interp(1), apply_motor(X, N, 2, E1, RG, O)));
  // (Computed with a float64 reference.)
  SCHIR_ASSERT(near(Interp(0.5), vec3{-2.7773230f, -1.5978639f, 3.0250815f}));
  SCHIR_ASSERT(near(apply_interp_3(X, E3, 1, E1, R90, C,
                                   N, 2, E1, RG, O, 1.0f / 3),
                    apply_motor(X, N, 2, E1, RG, O)));
  SCHIR_ASSERT(near(apply_interp_3(X, N, -1, E1, R270, O,
                                   E2, 4, E1, R90, C, 1.0f / 3),
                    apply_motor(X, E2, 4, E1, R90, C)));

  // A screw interpolates its angle and distance.
  auto Screw = [&](float t) {
    return Id(apply_interp, X, E3, 4, E1, R90, C, t);
  };
  SCHIR_ASSERT(near(Screw(0), X));
  SCHIR_ASSERT(near(Screw(0.5), rotate(X, E3, Pi / 4, C) + 2 * E3));
  SCHIR_ASSERT(near(Screw(0.25), rotate(X, E3, Pi / 8, C) + E3));
  SCHIR_ASSERT(near(Screw(1), rotate(X, E3, Pi / 2, C) + 4 * E3));
  SCHIR_ASSERT(near(Id(apply_interp, X, N, -6, E1, E1, C, 0.5f), X - 3 * N));

  // The rotation by 270° interpolates through the rotation by -90°.
  SCHIR_ASSERT(near(Id(apply_interp, X, E3, 0, E1, R270, C, 0.5f),
                    rotate(X, E3, -Pi / 4, C)));
  SCHIR_ASSERT(near(Id(apply_interp_3, X, E3, 0, E1, R270, C, 1.0f / 3),
                    rotate(X, E3, -Pi / 2, C)));

  // A half-turn with a scalar part that is zero at run time.
  SCHIR_ASSERT(near(Id(apply_interp, X, E3, 0, E1, E2, C, 0.5f),
                    rotate(X, E3, Pi / 2, C)));
  SCHIR_ASSERT(near(apply_interp_half_turn(X, 0.5f),
                    rotate(X, E3, -Pi / 2, O)));
  SCHIR_ASSERT(near(apply_interp_half_turn(X, 1), rotate(X, E3, Pi, O)));

  // The identity versor and the translation by zero.
  SCHIR_ASSERT(near(apply_interp_identity(X, R90, C, 0), X));
  SCHIR_ASSERT(near(apply_interp_identity(X, R90, C, 0.5f),
                    rotate(X, E3, Pi / 4, C)));
  constexpr vec3 T{2, 4, -6};
  SCHIR_ASSERT(near(apply_translation_interp(X, T, 0.5f), X + vec3{1, 2, -3}));
  SCHIR_ASSERT(near(apply_translation_interp(X, O, 0.5f), X));
  SCHIR_ASSERT(near(apply_translation_interp_tenth(X, T),
                    X + vec3{0.2f, 0.4f, -0.6f}));

  // The inverse of a motor that is not factored.
  SCHIR_ASSERT(near(undo_exp_log(X, N, 2, E1, RG, C), X));

  // Interpolating a motor to itself is the identity.
  SCHIR_ASSERT(near(apply_interp_self(X, N, 2, E1, RG, C, 0.5f),
                    apply_motor(X, N, 2, E1, RG, C)));

  // log(b a) = -θ/2 e₁₂ for the rotation by θ from e₁ to e₂.
  SCHIR_ASSERT(near(log_rotation_e12(E1, R90), -Pi / 4));
  SCHIR_ASSERT(near(log_rotation_e12(2 * E1, 3 * R90), -Pi / 4));
  SCHIR_ASSERT(near(log_rotation_e12(E1, R270), Pi / 4));
  SCHIR_ASSERT(near(log_rotation_e12(E1, E1), 0));

  // Exponentials of bivectors.
  SCHIR_ASSERT(near(apply_exp_rotation(X, E1, E2, Pi / 3),
                    rotate(X, E3, Pi / 3, O)));
  SCHIR_ASSERT(near(apply_exp_rotation(X, E2, E3, -2.5f),
                    rotate(X, E1, -2.5f, O)));
  SCHIR_ASSERT(near(apply_exp_rotation(X, E1, E2, 0), X));
  SCHIR_ASSERT(near(apply_exp_rotation(X, E1, E2, 1e-4f),
                    rotate(X, E3, 1e-4f, O)));
  constexpr vec3 T2{3, -1, 2};
  SCHIR_ASSERT(near(apply_exp_translation(X, T2), X + T2));
  SCHIR_ASSERT(near(apply_exp_screw(X, Pi / 3, 2),
                    rotate(X, E3, Pi / 3, O) + 2 * E3));
  SCHIR_ASSERT(near(apply_exp_screw(X, 0, 2), X + 2 * E3));

  // 2D motors rotate about c and then translate.
  constexpr vec2 Y{3, 1};
  constexpr vec2 C2{1, 1};
  constexpr vec2 U1{1, 0};
  constexpr vec2 U2{0, 1};
  constexpr vec2 Q90{1, 1};
  const vec2 Q270{R270.value.x, R270.value.y};
  const vec2 N2{0.6f, -0.8f};
  SCHIR_ASSERT(near(apply_exp_log_2d(Y, N2, 5, U1, U1, C2), Y + 5 * N2));
  SCHIR_ASSERT(near(apply_exp_log_2d(Y, U2, 0, U1, Q90, C2),
                    rotate(Y, Pi / 2, C2)));
  SCHIR_ASSERT(near(apply_exp_log_2d(Y, N2, -2, U1, Q90, C2),
                    rotate(Y, Pi / 2, C2) - 2 * N2));
  SCHIR_ASSERT(near(apply_exp_log_2d(Y, N2, -2, U1, Q270, C2),
                    apply_motor_2d(Y, N2, -2, U1, Q270, C2)));

  auto Interp2 = [&](float t) {
    return apply_interp_2d(Y, Q90, C2, N2, -2, Q270, U2, t);
  };
  SCHIR_ASSERT(near(Interp2(0), rotate(Y, Pi / 2, C2)));
  SCHIR_ASSERT(near(Interp2(1), apply_motor_2d(Y, N2, -2, U1, Q270, U2)));
  SCHIR_ASSERT(near(apply_interp_3_2d(Y, Q90, C2, N2, -2, Q270, U2,
                                      1.0f / 3),
                    apply_motor_2d(Y, N2, -2, U1, Q270, U2)));
  // Every argument of the motors is distinct.
  auto InterpMotors2 = [&](float t) {
    return apply_interp_motors_2d(Y, N2, 3, U1, Q90, C2,
                                  U2, -2, U2, Q270, Y, t);
  };
  SCHIR_ASSERT(near(InterpMotors2(0), apply_motor_2d(Y, N2, 3, U1, Q90, C2)));
  SCHIR_ASSERT(near(InterpMotors2(1), apply_motor_2d(Y, U2, -2, U2, Q270, Y)));
  SCHIR_ASSERT(near(apply_interp_motors_2d(Y, U2, 0, U1, Q90, C2,
                                           N2, -2, U1, Q270, U2, 0.3f),
                    Interp2(0.3f)));
  // Rotations about a point interpolate their angle.
  SCHIR_ASSERT(near(apply_interp_2d(Y, U1, C2, U2, 0, Q90, C2, 0.5f),
                    rotate(Y, Pi / 4, C2)));
  SCHIR_ASSERT(near(apply_interp_2d(Y, U1, C2, U2, 0, Q270, C2, 0.5f),
                    rotate(Y, -Pi / 4, C2)));
  SCHIR_ASSERT(near(apply_interp_2d(Y, U1, C2, N2, 4, U1, C2, 0.5f),
                    Y + 2 * N2));

  // A constant t = k / 2ⁿ uses square roots of motors.
  auto Motors = [&](auto f, auto ...rest) {
    return f(X, E3, 1, E1, R90, C, N, 2, E1, RG, O, rest...);
  };
  SCHIR_ASSERT(near(Motors(apply_interp_0), Interp(0)));
  SCHIR_ASSERT(near(Motors(apply_interp_1), Interp(1)));
  SCHIR_ASSERT(near(Motors(apply_interp_half), Interp(0.5f)));
  SCHIR_ASSERT(near(Motors(apply_interp_3_4), Interp(0.75f)));
  SCHIR_ASSERT(near(Motors(apply_interp_1_16), Interp(0.0625f)));
  // Pure rotations, screws, and the branch with a scalar part ≥ 0.
  auto IdHalf = [&](auto ...args) { return Id(apply_interp_half, X, args...); };
  SCHIR_ASSERT(near(IdHalf(E3, 4, E1, R90, C),
                    rotate(X, E3, Pi / 4, C) + 2 * E3));
  SCHIR_ASSERT(near(IdHalf(N, -6, E1, E1, C), X - 3 * N));
  SCHIR_ASSERT(near(IdHalf(E3, 0, E1, R270, C), rotate(X, E3, -Pi / 4, C)));
  SCHIR_ASSERT(near(IdHalf(E3, 0, E1, E2, C), rotate(X, E3, Pi / 2, C)));
  SCHIR_ASSERT(near(Id(apply_interp_3_4, X, E3, 4, E1, RG, C),
                    Id(apply_interp, X, E3, 4, E1, RG, C, 0.75f)));
  SCHIR_ASSERT(near(apply_interp_half_2d(Y, Q90, C2, N2, -2, Q270, U2),
                    Interp2(0.5f)));
  SCHIR_ASSERT(near(apply_interp_half_2d(Y, U1, C2, U2, 0, Q270, C2),
                    rotate(Y, -Pi / 4, C2)));

  // nlerp is the same as interp at t = 0, ½, and 1.
  SCHIR_ASSERT(near(Motors(apply_nlerp, 0), Interp(0)));
  SCHIR_ASSERT(near(Motors(apply_nlerp, 0.5f), Interp(0.5f)));
  SCHIR_ASSERT(near(Motors(apply_nlerp, 1), Interp(1)));
  SCHIR_ASSERT(near(Motors(apply_nlerp_half), Interp(0.5f)));
  SCHIR_ASSERT(near(Id(apply_nlerp, X, E3, 0, E1, R270, C, 0.5f),
                    rotate(X, E3, -Pi / 4, C)));
  // Otherwise the rotation by 2φ becomes the rotation by 2α with
  // tan α = t sin φ / ((1 - t) + t cos φ) and translations are linear.
  auto NlerpAngle = [](float Phi, float t) {
    return 2 * std::atan2(t * std::sin(Phi), (1 - t) + t * std::cos(Phi));
  };
  for (float t : std::array{0.1f, 0.3f, 0.7f}) {
    float Phi = Pi / 3;
    const vec3 RPhi{std::cos(Phi), std::sin(Phi), 0};
    SCHIR_ASSERT(near(Id(apply_nlerp, X, E3, 0, E1, RPhi, C, t),
                      rotate(X, E3, NlerpAngle(Phi, t), C)));
    SCHIR_ASSERT(near(Id(apply_nlerp, X, N, 5, E1, E1, C, t), X + 5 * t * N));
    SCHIR_ASSERT(near(apply_nlerp_2d(Y, U1, C2, U2, 0, Q90, C2, t),
                      rotate(Y, NlerpAngle(Pi / 4, t), C2)));
  }
  SCHIR_ASSERT(near(apply_nlerp_2d(Y, Q90, C2, N2, -2, Q270, U2, 1),
                    Interp2(1)));

  // Normalizing a scaled motor keeps its motion.
  SCHIR_ASSERT(near(apply_normalize(X, -3, N, 2, E1, RG, C),
                    apply_motor(X, N, 2, E1, RG, C)));
  SCHIR_ASSERT(near(apply_normalize(X, 0.25f, E3, 4, E1, R270, C),
                    apply_motor(X, E3, 4, E1, R270, C)));
}
