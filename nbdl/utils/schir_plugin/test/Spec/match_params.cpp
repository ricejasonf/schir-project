// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
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

namespace {
namespace foo {

struct my_struct {
  int value = 0;
};

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp context
            scale_foo
            sum_foo_bar)

(define-context context (arg1 arg2)
  (member: '.foo 'int (init-args: arg1))
  (member: '.bar 'int (init-args: arg2)))

(define-match-fn scale_foo (context (Factor : 'int) fn)
  (visit fn (visit '|std::multiplies<int>{}| (get context '.foo) Factor)))

(define-match-fn sum_foo_bar (context fn)
  (match-params ((A (get context '.foo))
                 (B : 'int (get context '.bar)))
    (visit fn (visit '|std::plus<int>{}| A B))))

(export-cpp match_int
            match_my_struct
            match_with_match_params)

; // CHECK-LABEL: @"::foo::match_int"
; // CHECK: "nbdl.match"(%arg{{[0-9]+}})
; // CHECK-NEXT: ({{%arg[0-9]+}}: !nbdl.store<!nbdl.cpp<"int">>):
(define-match-fn match_int ((V : 'int) fn)
  (visit fn V))

; // CHECK-LABEL: @"::foo::match_my_struct"
; // CHECK: "nbdl.match"(%arg{{[0-9]+}})
; // CHECK-NEXT: ({{%arg[0-9]+}}: !nbdl.store<!nbdl.cpp<"foo::my_struct">>):
(define-match-fn match_my_struct ((V : 'foo::my_struct) fn)
  (visit fn V))

; // CHECK-LABEL: @"::foo::match_with_match_params"
; // CHECK: "nbdl.match"(%arg{{[0-9]+}})
; // CHECK-NEXT: ({{%arg[0-9]+}}: !nbdl.store<!nbdl.cpp<"foo::my_struct">>):
(define-match-fn match_with_match_params (Store fn)
  (match-params ((V : 'foo::my_struct Store))
    (visit fn V)))

(write-nbdl-module)

}
}  // namespace foo
}  // namespace

int main() {
  auto ctx = foo::context(6, 7);

  int scaled = 0;
  foo::scale_foo(ctx, 7, [&](int v) {
    scaled = v;
  });
  SCHIR_ASSERT(scaled == 42);

  int sum = 0;
  foo::sum_foo_bar(ctx, [&](int v) {
    sum = v;
  });
  SCHIR_ASSERT(sum == 13);
}
