// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_TAIL_SCOPE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_TAIL_RESULT -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_TAIL_MATCH_EACH -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NO_BASE_CASE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NO_MATCH -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_LOOP_TYPE_MISMATCH -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Check recursive visits when a match-fn is defined.
// Expect the error at the name of the definition with
// notes at the locations of the invalid recursive visits.

#ifdef TEST_NOT_TAIL_SCOPE
// expected-error@+8 {{invalid recursive visit}}
// expected-note@+11 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
; // Follow the recursive visit with another visit
; // (ie put it in a nbdl.scope.)
(define-match-fn not_tail (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (begin
      (visit not_tail (visit '|std::minus<int>{}| N '|int{1}|) Fn)
      (visit Fn N))))
}
#endif

#ifdef TEST_NOT_TAIL_RESULT
// expected-error@+6 {{invalid recursive visit}}
// expected-note@+8 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Use the result of the recursive visit.
(define-match-fn not_tail (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit Fn (visit not_tail (visit '|std::minus<int>{}| N '|int{1}|)
                              Fn))))
}
#endif

#ifdef TEST_NOT_TAIL_MATCH_EACH
// expected-error@+6 {{invalid recursive visit}}
// expected-note@+7 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Invoke the recursive visit for each element.
(define-match-fn not_tail (Range Fn)
  (match-each Range
    (lambda (X) (visit not_tail X Fn))))
}
#endif

#ifdef TEST_NO_BASE_CASE
// expected-error@+6 {{invalid recursive visit}}
// expected-note@+5 {{recursive function has no base case (ie every branch visits the function)}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Visit the function in every branch of the match-if.
(define-match-fn no_base (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit no_base N Fn)
    (visit no_base (visit '|std::minus<int>{}| N '|int{1}|) Fn)))
}
#endif

#ifdef TEST_NO_MATCH
// expected-error@+6 {{invalid recursive visit}}
// expected-note@+6 {{recursive visit must be in a match or match_if to have a base case}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Visit the function unconditionally.
(define-match-fn no_match (N Fn)
  (visit no_match N Fn))
}
#endif

#ifdef TEST_LOOP_TYPE_MISMATCH
// Expect finalize-module to raise errors from the inference passes.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+10 {{'nbdl.continue' op type of loop-carried value 1 ('!nbdl.store<!nbdl.cpp<"long">>') does not match the type of the loop argument ('!nbdl.store<!nbdl.cpp<"int">>')}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp sum_to)
; // Change the type of a loop-carried value between iterations.
; // (Start Acc as an int, but get a long from std::plus<long>.)
(define-match-fn sum_down (N Acc Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn Acc)
    (visit sum_down (visit '|std::minus<int>{}| N '|int{1}|)
                    (visit '|std::plus<long>{}| Acc N)
                    Fn)))
(define-fn sum_to ((N : 'int) (Fn : 'nbdl::noop_t)) -> ()
  (visit sum_down N '|int{0}| Fn)
  (return))
(finalize-module)
}
#endif
