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
        (geomalg base))

(export-c test_basic_sum_0 test_basic_sum_1 test_basic_sum_2
          test_vec_sum_1 test_vec_sum_2 test_vec_sum_3 test_negate)

(define-fn test_basic_sum_0 () -> (!nbdl.unknown)
  (return (sum)))

(define-fn test_basic_sum_1 ((a : !e1)) -> (!nbdl.unknown)
  (return (sum a)))

(define-fn test_basic_sum_2 ((a : !e2) (b : !e2)) -> (!nbdl.unknown)
  (return (sum a b (e2 64))))

(define-fn test_vec_sum_1 ((a : !vec2)) -> (!nbdl.unknown)
  (return (sum a)))

(define-fn test_vec_sum_2 ((a : !vec2) (b : !vec2)) -> (!nbdl.unknown)
  (return (sum a b)))

(define-fn test_vec_sum_3 ((a : !vec2) (b : !vec2) (c : !vec3))
                           -> (!nbdl.unknown)
  (return (sum a b c)))

(define-fn test_negate ((a : !vec2)) -> (!nbdl.unknown)
  (return (sum a (negate a))))

(finalize-module)
}
} // namespace test
} // namespace

using namespace test;
using geomalg::near;
using geomalg::vec2;
using geomalg::vec3;

int main() {
  SCHIR_ASSERT(test_basic_sum_0().value == 0);
  SCHIR_ASSERT(test_basic_sum_1(geomalg::e1{128}).value == 128);
  SCHIR_ASSERT(test_basic_sum_2(geomalg::e2{128}, geomalg::e2{64}).value ==
               256);
  SCHIR_ASSERT(near(test_vec_sum_1(vec2{8, 16}), vec2{8, 16}, 0));
  SCHIR_ASSERT(near(test_vec_sum_2(vec2{2, 32}, vec2{2, 32}), vec2{4, 64}, 0));
  SCHIR_ASSERT(near(test_vec_sum_2(vec2{2, 32}, vec2{-2, -32}), vec2{0, 0},
                    0));
  SCHIR_ASSERT(near(test_vec_sum_3(vec2{2, 32}, vec2{-2, -32},
                                   vec3{3.14, -3.14, 5}),
                    vec3{3.14, -3.14, 5}, 0));
  SCHIR_ASSERT(test_negate(vec2{5, 6}).value == 0.0);
}
