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
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_TAIL_CONTINUATION -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NO_BASE_CASE_MUTUAL -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_CHANGING_FN -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_CHANGING_CLOSURE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_CLOSURE_NOT_TAIL -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_TAIL_IN_CLOSURE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NO_BASE_CASE_CLOSURE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_REENTER_VIA_CLOSURE -fsyntax-only -Xclang -verify %s

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

#ifdef TEST_NOT_TAIL_CONTINUATION
// Detect recursion via a function passed as an argument when inlining.
// Expect finalize-module to raise errors from the inference passes.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+10 {{recursive visit must be in tail position}}
// expected-note@+17 {{the function is visited here}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
(export-cpp run)
; // Visit Fn twice (ie not in tail position.)
(define-match-fn visit_twice (N Fn Sink)
  (begin
    (visit Fn N Sink)
    (visit Fn N Sink)))
; // Visit walk recursively via visit_twice.
(define-match-fn walk (N Sink)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Sink N)
    (visit visit_twice (visit '|std::minus<int>{}| N '|int{1}|) walk Sink)))
(define-fn run ((N : 'int) (Sink : 'nbdl::noop_t)) -> ()
  (visit walk N Sink)
  (return))
(finalize-module)
}
#endif

#ifdef TEST_NO_BASE_CASE_MUTUAL
// Detect no base case for functions that visit each other.
// (Expect the error within the definition of ping as well.)
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+9 {{recursive visit has no base case (ie every branch continues a loop)}}
// expected-note@+12 {{recursive visit has no base case (ie every branch continues a loop)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
(declare-match-fn pong)
; // Visit each function from the other in every branch.
(define-match-fn ping (N)
  (visit pong N))
(define-match-fn pong (N)
  (visit ping N))
(define-fn run ((N : 'int)) -> ()
  (visit ping N)
  (return))
(finalize-module)
}
#endif

#ifdef TEST_CHANGING_FN
// Require a function passed to a recursive visit to be the same in every
// iteration since a function is not a value that a loop can carry.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+13 {{recursive visit must pass the same function, member name, or closure as argument 1}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
(define-match-fn finish (N)
  (visit '|nbdl::noop_t{}| N))
(define-match-fn finish_other (N)
  (visit '|nbdl::noop_t{}| N))
; // Pass a different function in the recursive visit.
(define-match-fn count_down (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down (visit '|std::minus<int>{}| N '|int{1}|)
                      finish_other)))
(define-fn run ((N : 'int)) -> ()
  (visit count_down N finish)
  (return))
(finalize-module)
}
#endif

#ifdef TEST_CHANGING_CLOSURE
// Require a closure passed to a recursive visit to be the same.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+9 {{recursive visit must pass the same function, member name, or closure as argument 1}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
; // Pass a new closure in the recursive visit.
(define-match-fn count_down (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down (visit '|std::minus<int>{}| N '|int{1}|)
                      (match-fn (X) (visit Fn X)))))
(define-fn run ((N : 'int)) -> ()
  (visit count_down N (match-fn (X) (visit '|nbdl::noop_t{}| X)))
  (return))
(finalize-module)
}
#endif

#ifdef TEST_CLOSURE_NOT_TAIL
// Require each visit on a closure to be in tail position.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+6 {{closure is not inlined (ie each visit on it must be in tail position)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
(define-fn run ((N : 'int)) -> ()
  (match-if (visit (match-fn (X) (visit '|nbdl::noop_t{}| X)) N)
    (visit '|nbdl::noop_t{}| N)
    (visit '|nbdl::noop_t{}| N))
  (return))
(finalize-module)
}
#endif

#ifdef TEST_NOT_TAIL_IN_CLOSURE
// Check a recursive visit within a closure when the closure is inlined.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+12 {{recursive visit must be in tail position}}
// expected-note@+15 {{the function is visited here}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
(export-cpp run)
(define-match-fn walk (N Sink)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Sink N)
    (visit (match-fn (X)
             (begin
               (visit walk X Sink)
               (visit Sink X)))
           (visit '|std::minus<int>{}| N '|int{1}|))))
(define-fn run ((N : 'int) (Sink : 'nbdl::noop_t)) -> ()
  (visit walk N Sink)
  (return))
(finalize-module)
}
#endif

#ifdef TEST_NO_BASE_CASE_CLOSURE
// Check the base case of a recursive visit through a closure.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+8 {{recursive visit has no base case (ie every branch continues a loop)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
(define-match-fn forever (N)
  (visit (match-fn (X) (visit forever X)) N))
(define-fn run ((N : 'int)) -> ()
  (visit forever N)
  (return))
(finalize-module)
}
#endif

#ifdef TEST_REENTER_VIA_CLOSURE
// Re-enter a match-fn via a closure as a continue of its loop.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+11 {{'nbdl.continue' op type of loop-carried value 1 ('!nbdl.store<!nbdl.cpp<"nbdl::noop_t">>') does not match the type of the loop argument ('!nbdl.store<!nbdl.closure>')}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run)
(define-match-fn count_down (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down (visit '|std::minus<int>{}| N '|int{1}|) Fn)))
(define-fn run ((N : 'int)) -> ()
  (visit count_down N
         (match-fn (X) (visit count_down X '|nbdl::noop_t{}|)))
  (return))
(finalize-module)
}
#endif
