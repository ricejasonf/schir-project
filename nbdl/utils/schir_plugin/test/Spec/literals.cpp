// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <type_traits>

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp
  test_int
  test_float
  test_negative_float
  test_match_int
  test_match_float)

; // Literals are lifted to their corresponding C++ types.
(define-match-fn test_int (fn)
  (visit fn 42))

(define-match-fn test_float (fn)
  (visit fn 3.14))

(define-match-fn test_negative_float (fn)
  (visit fn -2.5))

; // Literal types match their corresponding C++ types.
(define-match-fn test_match_int (fn)
  (match 5
    ('float => (lambda (x) (visit fn 1.5)))
    ('int32_t => fn)
    (else => (lambda (x) (visit fn 2)))))

(define-match-fn test_match_float (fn)
  (match 3.14
    ('int32_t => (lambda (x) (visit fn 1)))
    ('float => fn)
    (else => (lambda (x) (visit fn 2)))))

} // schir_scheme

int main() {
  bool result_int = false;
  test_int([&](auto x) {
    result_int = std::is_same_v<decltype(x), int> && x == 42;
  });
  SCHIR_ASSERT(result_int);

  bool result_float = false;
  test_float([&](auto x) {
    result_float = std::is_same_v<decltype(x), float> && x == 3.14f;
  });
  SCHIR_ASSERT(result_float);

  bool result_negative_float = false;
  test_negative_float([&](auto x) {
    result_negative_float = std::is_same_v<decltype(x), float> && x == -2.5f;
  });
  SCHIR_ASSERT(result_negative_float);

  bool result_match_int = false;
  test_match_int([&](auto x) {
    result_match_int = std::is_same_v<decltype(x), int> && x == 5;
  });
  SCHIR_ASSERT(result_match_int);

  bool result_match_float = false;
  test_match_float([&](auto x) {
    result_match_float = std::is_same_v<decltype(x), float> && x == 3.14f;
  });
  SCHIR_ASSERT(result_match_float);
}
