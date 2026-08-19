// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_FOO_FAIL -fsyntax-only -Xclang -verify %s

// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <optional>

namespace my {
struct foo {
  void get_void() { }
};

struct bar {
  int get_five() {
    return 5;
  }

  int get_five_plus(int x) {
    return 5 + x;
  }

  std::optional<int> maybe_five(int x) {
    if (x == 5)
      return {5};
    else
      return {};
  }
};

int sum(int a, int b) {
  return a + b;
}

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp
  test_1
  test_2
  test_3
  test_4
  test_5)

; // Nullary void returning member function
(match-params-fn test_1 (store fn)
  (match-cond
    ((sfinae-visit '.get_void store)
     (visit fn 42))
    (else (visit fn 1))))

; // Nullary int returning member function
(match-params-fn test_2 (store fn)
  (define (GetFive Store)
    (sfinae-visit '.get_five Store))
  (match-cond
    ((GetFive store)
     (match (GetFive store) ; // valid within region
      (else => (lambda (val) (visit fn val)))))
    (else (visit fn 1))))

; // Not in SFINAE context (ie not conditional)
(match-params-fn test_3 (store fn)
  (define (GetFive Store)
    (sfinae-visit '.get_five Store))
  (match (GetFive store)
    (else => fn))) // Fails for foo but not bar.

(match-params-fn test_4 (store x fn)
  (define (GetVoid Store)
    (sfinae-visit '.get_void Store))
  (define (GetFivePlus Store X)
    (sfinae-visit '.get_five_plus Store X))
  (match-cond
    ((GetVoid store)
     (visit fn 1))
    ((GetFivePlus store -5) => fn) ; // 5 - 5 = 0 so fall through
    ((GetFivePlus store x) => fn)
    (else (visit fn 1000))))

(match-params-fn test_5 (store x fn)
  (define (MaybeFive Store X)
    (sfinae-visit '.maybe_five Store X))
  (match-cond
   ((MaybeFive store 4)
    (visit fn 1))
   ((MaybeFive store 6)
    (visit fn 2))
   ((MaybeFive store 5)
    (match-cond
     ((sfinae-visit 'my::sum x "moo")
       (visit fn 3))
     ((sfinae-visit 'my::sum x 12)
       (visit fn (visit 'my::sum x 12))) ; // Sorry
     (else (visit fn 4))))
   (else (visit fn (visit 'my::sum x 100)))))

} // namespace my
} // namespace

int main() {
  {
    my::foo foo;
    int result_1 = 0;
    my::bar bar;
    int result_2 = 0;
    my::test_1(foo, [&](int result) {
      result_1 = result;
    });
    my::test_1(bar, [&](int result) {
      result_2 = result;
    });
    SCHIR_ASSERT(result_1 == 42);
    SCHIR_ASSERT(result_2 == 1);
  }

  {
    my::foo foo;
    int result_1 = 0;
    my::bar bar;
    int result_2 = 0;
    int result_3 = 0;
    my::test_2(foo, [&](int result) {
      result_1 = result;
    });
    my::test_2(bar, [&](int result) {
      result_2 = result;
    });
#ifdef TEST_FOO_FAIL
    // expected-error@* {{no member named 'sfinae_result_value'}}
    // expected-note@* 2 {{in instantiation of function template specialization}}
    // expected-note@+1 {{in instantiation of function template specialization}}
    my::test_3(foo, [&](int result) {
      result_3 = result;
    });
#endif
    my::test_3(bar, [&](int result) {
      result_3 = result;
    });
    SCHIR_ASSERT(result_1 == 1);
    SCHIR_ASSERT(result_2 == 5);
    SCHIR_ASSERT(result_3 == 5);
  }

  {
    my::foo foo;
    int result_1 = 0;
    my::bar bar;
    int result_2 = 0;
    my::test_4(foo, 2, [&](int result) {
      result_1 = result;
    });
    my::test_4(bar, 2, [&](int result) {
      result_2 = result;
    });
    SCHIR_ASSERT(result_1 == 1);
    SCHIR_ASSERT(result_2 == 7);
  }

  {
    my::foo foo;
    int result_1 = 0;
    my::bar bar;
    int result_2 = 0;
    my::test_5(foo, 23, [&](int result) {
      result_1 = result;
    });
    my::test_5(bar, 23, [&](int result) {
      result_2 = result;
    });
    SCHIR_ASSERT(result_1 == 100 + 23);
    SCHIR_ASSERT(result_2 == 23 + 12);
  }  
}
