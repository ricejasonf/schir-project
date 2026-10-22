// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

// The scalar functions are lowered to our own polynomials
// (instead of libm) so check them against libm in double precision.

#include <geomalg/nbdl.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace {
namespace test {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec geomalg)
        (geomalg base)
        (only (schir mlir) create-op result))

(export-c test_sin test_cos test_atan2)

(define (scalar-op-aux Loc Name . Args)
  (result
    (create-op Name
               (loc: Loc)
               (operands: Args)
               (attributes:)
               (result-types: !scalar))))

(define-syntax scalar-op
  (syntax-rules ()
    ((scalar-op Name Arg ...)
     (scalar-op-aux (syntax-source-loc) Name Arg ...))))

(define-fn test_sin ((x : !scalar)) -> (!scalar)
  (return (scalar-op "geomalg.sin" x)))

(define-fn test_cos ((x : !scalar)) -> (!scalar)
  (return (scalar-op "geomalg.cos" x)))

(define-fn test_atan2 ((y : !scalar) (x : !scalar)) -> (!scalar)
  (return (scalar-op "geomalg.atan2" y x)))

(finalize-module)
}
} // namespace test

// Compare the coefficients of the scalar results as floats.
float test_sin(float x) { return test::test_sin(x).value; }
float test_cos(float x) { return test::test_cos(x).value; }
float test_atan2(float y, float x) { return test::test_atan2(y, x).value; }
} // namespace

// The error of the result in units in the last place of the exact value
// (with the ulp of the smallest normal float near zero.)
inline double ulp_error(float Result, double Exact) {
  float F = static_cast<float>(Exact);
  double Ulp = std::nextafter(std::max(std::abs(F), 0x1p-126f),
                              INFINITY) - std::max(std::abs(F), 0x1p-126f);
  return std::abs(Result - Exact) / Ulp;
}

// The absolute error relative to the ulp of 1 (2⁻²³) since the
// error of the reduction of a large argument is absolute.
inline double abs_error(float Result, double Exact) {
  return std::abs(Result - Exact) / 0x1p-23;
}

int main() {
  constexpr float Pi = std::numbers::pi_v<float>;

  // Exact values.
  SCHIR_ASSERT(test_sin(0.0f) == 0.0f && !std::signbit(test_sin(0.0f)));
  SCHIR_ASSERT(test_sin(-0.0f) == 0.0f && std::signbit(test_sin(-0.0f)));
  SCHIR_ASSERT(test_cos(0.0f) == 1.0f);
  SCHIR_ASSERT(test_cos(-0.0f) == 1.0f);
  SCHIR_ASSERT(test_atan2(0.0f, 1.0f) == 0.0f);
  SCHIR_ASSERT(std::signbit(test_atan2(-0.0f, 1.0f)));
  SCHIR_ASSERT(test_atan2(0.0f, 0.0f) == 0.0f);
  SCHIR_ASSERT(test_atan2(0.0f, -0.0f) == Pi);
  SCHIR_ASSERT(test_atan2(-0.0f, -0.0f) == -Pi);
  SCHIR_ASSERT(test_atan2(0.0f, -1.0f) == Pi);
  SCHIR_ASSERT(test_atan2(-0.0f, -1.0f) == -Pi);
  SCHIR_ASSERT(test_atan2(1.0f, 0.0f) == Pi / 2);
  SCHIR_ASSERT(test_atan2(-1.0f, -0.0f) == -Pi / 2);
  SCHIR_ASSERT(test_atan2(1.0f, 1.0f) == Pi / 4);
  SCHIR_ASSERT(test_atan2(-2.0f, -2.0f) == -3 * Pi / 4);

  // Within π/4 of zero the error is relative.
  double MaxSin = 0, MaxCos = 0;
  for (float x = -Pi / 4; x <= Pi / 4; x += 0x1p-14f) {
    MaxSin = std::max(MaxSin, ulp_error(test_sin(x), std::sin(double(x))));
    MaxCos = std::max(MaxCos, ulp_error(test_cos(x), std::cos(double(x))));
  }
  SCHIR_ASSERT(MaxSin <= 1 && MaxCos <= 1);
  for (float x = 0x1p-30f; x < 0.1f; x *= 1.01f) {
    SCHIR_ASSERT(ulp_error(test_sin(x), std::sin(double(x))) <= 1);
    SCHIR_ASSERT(ulp_error(test_sin(-x), std::sin(-double(x))) <= 1);
  }

  // Elsewhere the error is absolute and grows with the argument.
  MaxSin = 0, MaxCos = 0;
  for (float x = -100; x <= 100; x += 0x1p-10f) {
    MaxSin = std::max(MaxSin, abs_error(test_sin(x), std::sin(double(x))));
    MaxCos = std::max(MaxCos, abs_error(test_cos(x), std::cos(double(x))));
  }
  SCHIR_ASSERT(MaxSin <= 1 && MaxCos <= 1);

  // atan2 in every octant
  double MaxAtan2 = 0;
  for (float a = -Pi; a <= Pi; a += 0x1p-12f) {
    for (float r : {0x1p-20f, 1.0f, 3.0f, 0x1p20f}) {
      float y = r * std::sin(a);
      float x = r * std::cos(a);
      MaxAtan2 = std::max(MaxAtan2, ulp_error(test_atan2(y, x),
                                     std::atan2(double(y), double(x))));
    }
  }
  SCHIR_ASSERT(MaxAtan2 <= 2);
  for (float y = 0x1p-30f; y < 1; y *= 1.01f) {
    SCHIR_ASSERT(ulp_error(test_atan2(y, 1), std::atan2(double(y), 1)) <= 2);
    SCHIR_ASSERT(ulp_error(test_atan2(-y, 1), std::atan2(-double(y), 1)) <= 2);
  }
}
