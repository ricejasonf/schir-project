// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace my {
enum ref_kind { lvalue = 1, const_lvalue = 2, rvalue = 3 };

struct inner {
  ref_kind which() & { return lvalue; }
  ref_kind which() const& { return const_lvalue; }
};

struct holder {
  int value = 0;
  my::inner inner;
};

struct my_tag { };

ref_kind which(int&) { return lvalue; }
ref_kind which(int const&) { return const_lvalue; }

ref_kind tag_kind(my_tag&) { return lvalue; }
ref_kind tag_kind(my_tag const&) { return const_lvalue; }
ref_kind tag_kind(my_tag&&) { return rvalue; }

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp
  test_free
  test_member
  test_tag
  test_sfinae_tag)

; // Pass an unmarked operand as const and a mut! operand as mutable.
(define-match-fn test_free (store fn)
  (visit fn (visit 'my::which (get store '.value))
            (visit 'my::which (mut! (get store '.value)))))

; // Call the const overload of a member on an unmarked object.
(define-match-fn test_member (store fn)
  (visit fn (visit '.which (get store '.inner))
            (visit '.which (mut! (get store '.inner)))))

; // Write a tag operand as a prvalue.
(define-match-fn test_tag (fn)
  (visit fn (visit 'my::tag_kind (tag '|my::my_tag{}|))))

; // Write a tag operand of a sfinae visit as a prvalue.
(define-match-fn test_sfinae_tag (fn)
  (match-cond
    ((sfinae-visit 'my::tag_kind (tag '|my::my_tag{}|)) => fn)
    (else (visit fn 0))))

(finalize-module)
}
} // namespace my

int main() {
  my::holder holder;
  int result_1 = 0;
  int result_2 = 0;

  my::test_free(holder, [&](my::ref_kind x, my::ref_kind y) {
    result_1 = x;
    result_2 = y;
  });
  SCHIR_ASSERT(result_1 == my::const_lvalue);
  SCHIR_ASSERT(result_2 == my::lvalue);

  my::test_member(holder, [&](my::ref_kind x, my::ref_kind y) {
    result_1 = x;
    result_2 = y;
  });
  SCHIR_ASSERT(result_1 == my::const_lvalue);
  SCHIR_ASSERT(result_2 == my::lvalue);

  result_1 = 0;
  my::test_tag([&](my::ref_kind x) { result_1 = x; });
  SCHIR_ASSERT(result_1 == my::rvalue);

  result_1 = 0;
  my::test_sfinae_tag([&](int x) { result_1 = x; });
  SCHIR_ASSERT(result_1 == my::rvalue);
}
