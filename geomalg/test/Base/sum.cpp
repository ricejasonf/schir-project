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
using vec4 = vec<4>;

template <unsigned n>
constexpr inline bool vec_equal(vec<n> a, vec<n> b) {
  return __builtin_reduce_and(a == b);
}

extern "C" float test_basic_sum_0();
extern "C" float test_basic_sum_1(float);
extern "C" float test_basic_sum_2(float, float);
extern "C" vec2 test_vec_sum_1(vec2);
extern "C" vec2 test_vec_sum_2(vec2, vec2);
extern "C" vec2 test_vec_sum_3(vec2, vec2, vec2);
extern "C" float test_negate(vec2);
extern "C" float test_compose(vec2);

#pragma schir_scheme
{
(import (schir base)
        (schir llvm pass)
        (schir mlir all-passes)
        (geomalg base))

(define-func test_basic_sum_0 ()
  (sum))

(define-func test_basic_sum_1 ((a : !e1))
  (sum a))

(define-func test_basic_sum_2 ((a : !e2) (b : !e2))
  (sum a b (e2 64)))

(define-func test_vec_sum_1 ((a : !vec2))
  (sum a))

(define-func test_vec_sum_2 ((a : !vec2) (b : !vec2))
  (sum a b))

(define-func test_vec_sum_3 ((a : !vec2) (b : !vec2) (c : !vec3))
  (sum a b c))

(define-func test_negate ((a : !vec2))
  (sum a (negate a)))

#| // TODO Support internal calls.
(define-func test_compose ((a : !vec2))
  (sum (test_vec_sum2 a (test_basic_sum_1 (negate_ a)))
       (test_bacis_sum_0)
       (test_vec_sum_2 a (sum (e1 2) (e2 32)))))
|#

(run-passes geomalg-current-module
            "geomalg-to-llvm")
(inject-module geomalg-current-module)
}

int main() {
  SCHIR_ASSERT(test_basic_sum_0() == 0);
  SCHIR_ASSERT(test_basic_sum_1(128) == 128);
  SCHIR_ASSERT(test_basic_sum_2(128, 64) == 256);
  SCHIR_ASSERT(vec_equal(test_vec_sum_1(vec2{8, 16}),
                         vec2{8, 16}));
  SCHIR_ASSERT(vec_equal(test_vec_sum_2(vec2{2, 32}, vec2{2, 32}),
                         vec2{4, 64}));
  SCHIR_ASSERT(vec_equal(test_vec_sum_2(vec2{2, 32}, vec2{-2, -32}),
                         vec2{0, 0}));
  SCHIR_ASSERT(vec_equal(test_vec_sum_3(vec2{2, 32}, vec2{-2, -32},
                                        vec2{3.14, -3.14}),
                         vec2{3.14, -3.14}));
  SCHIR_ASSERT(test_negate(vec2{5, 6}) == 0.0);
#if 0 // TODO Support calls.
  SCHIR_ASSERT(vec_equal(test_compose(vec2{4, 32}),
                            vec2{8, 64}));
#endif
}
