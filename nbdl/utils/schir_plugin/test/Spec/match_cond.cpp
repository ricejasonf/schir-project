// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

//
// Copyright Jason Rice 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>
#include <nbdl/ext/std/tuple_like.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <functional>
#include <string>

namespace {
namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec))

  (export-cpp
    weighted_string
    context
    combo_concat
    last_arrow
    bare_test)

  (define (>= a b)
    (visit '|std::greater_equal<int>{}| a b))

  ; // Note that .first and .second are not accessible
  ; // because of implementation issues. (ie not having reflection)
  (define-store weighted_string ()
    (store "std::pair<int, std::string>"))

  ; // Store 3 values and the result of concatenating
  ; // the strings that match our conditional.
  (define-store context (arg1 arg2 arg3)
    (store-compose '.val1 (store 'weighted_string
                            (init-args: arg1)))
    (store-compose '.val2 (store 'weighted_string
                            (init-args: arg2)))
    (store-compose '.val3 (store 'weighted_string
                            (init-args: arg3)))
    (store-compose '.result_val (store 'std::string)))

  ; // Concatenate values with keys greater than 42.
  (define-match-fn combo_concat (context fn)
    ; // Concat a member (of context) to result_val
    ; // capitalizing it if its weight is greater than
    ; // the threshold 42.
    (define result-val
      (get context '.result_val))
    (define (my-concat member)
      ;; // This looks like destructuring here.
      (define weight
        (get member 'boost::hana::first))
      (define str
        (get member 'boost::hana::second))
      (match-cond
        ((>= weight 42)
         (visit '.append result-val str))
        (else (visit 'nbdl::noop))
        ))

    (visit '.clear result-val)
    (my-concat (get context '.val1))
    (my-concat (get context '.val2))
    (my-concat (get context '.val3))
    (visit fn 5))

  ; // Visit nothing when the last => clause does not match.
  (define-match-fn last_arrow (n fn)
    (match-cond
      ((>= n 42) => (match-fn (x) (visit fn n)))))

  ; // Skip the clauses after a bare test that matches.
  (define-match-fn bare_test (n fn)
    (match-cond
      ((>= n 42))
      (else (visit fn n))))

  ;; // See sfinae_visit.cpp for more tests on =>.

  (finalize-module)

}
}  // namespace foo
}  // namespace

static_assert(nbdl::State<foo::weighted_string>);
static_assert(nbdl::StdTupleLike<std::pair<int, std::string> &>);

int main() {

  foo::context ctx1(foo::weighted_string{0, "foo"},
                    foo::weighted_string{0, "bar"},
                    foo::weighted_string{0, "baz"});
  foo::context ctx2(foo::weighted_string{43, "foo"},
                    foo::weighted_string{5, "bar"},
                    foo::weighted_string{100, "baz"});
  foo::context ctx3(foo::weighted_string{0, "foo"},
                    foo::weighted_string{5, "bar"},
                    foo::weighted_string{100, "baz"});
  foo::context ctx4(foo::weighted_string{43, "foo"},
                    foo::weighted_string{9000, "bar"},
                    foo::weighted_string{100, "baz"});
  foo::combo_concat(ctx1, nbdl::noop);
  foo::combo_concat(ctx2, nbdl::noop);
  foo::combo_concat(ctx3, nbdl::noop);
  foo::combo_concat(ctx4, nbdl::noop);
  SCHIR_ASSERT(ctx1.result_val == std::string());
  SCHIR_ASSERT(ctx2.result_val == std::string("foobaz"));
  SCHIR_ASSERT(ctx3.result_val == std::string("baz"));
  SCHIR_ASSERT(ctx4.result_val == std::string("foobarbaz"));

  int result = 0;
  auto set_result = [&](int x) { result = x; };
  foo::last_arrow(5, set_result);
  SCHIR_ASSERT(result == 0);
  foo::last_arrow(43, set_result);
  SCHIR_ASSERT(result == 43);
  result = 0;
  foo::bare_test(43, set_result);
  SCHIR_ASSERT(result == 0);
  foo::bare_test(5, set_result);
  SCHIR_ASSERT(result == 5);
}
