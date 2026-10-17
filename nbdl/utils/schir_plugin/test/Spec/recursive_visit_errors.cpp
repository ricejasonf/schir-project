// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Check recursive visits when a match-fn is defined.
// Expect the error at the name of the definition with
// notes at the locations of the invalid recursive visits.

// expected-error@+8 {{invalid recursive visit}}
// expected-note@+11 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
; // Follow the recursive visit with another visit
; // (ie put it in a nbdl.scope.)
(define-match-fn not_tail_scope (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (begin
      (visit not_tail_scope (visit '|std::minus<int>{}| N '|int{1}|) Fn)
      (visit Fn N))))
}

// expected-error@+6 {{invalid recursive visit}}
// expected-note@+8 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Use the result of the recursive visit.
(define-match-fn not_tail_result (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit Fn (visit not_tail_result (visit '|std::minus<int>{}| N '|int{1}|)
                              Fn))))
}

// expected-error@+6 {{invalid recursive visit}}
// expected-note@+7 {{recursive visit must be in tail position}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Invoke the recursive visit for each element.
(define-match-fn not_tail_match_each (Range Fn)
  (match-each Range
    (lambda (X) (visit not_tail_match_each X Fn))))
}

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

// expected-error@+6 {{invalid recursive visit}}
// expected-note@+6 {{recursive visit must be in a match or match_if to have a base case}}
#pragma schir_scheme
{
(import (nbdl spec))
; // Visit the function unconditionally.
(define-match-fn no_match (N Fn)
  (visit no_match N Fn))
}

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

// Detect recursion via a function passed as an argument when inlining.
// Expect finalize-module to raise errors from the inference passes.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+10 {{recursive visit must be in tail position}}
// expected-note@+17 {{the function is visited here}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
(export-cpp run_continuation)
; // Visit Fn twice (ie not in tail position.)
(define-match-fn visit_twice (N Fn Sink)
  (begin
    (visit Fn N Sink)
    (visit Fn N Sink)))
; // Visit walk_continuation recursively via visit_twice.
(define-match-fn walk_continuation (N Sink)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Sink N)
    (visit visit_twice (visit '|std::minus<int>{}| N '|int{1}|) walk_continuation Sink)))
(define-fn run_continuation ((N : 'int) (Sink : 'nbdl::noop_t)) -> ()
  (visit walk_continuation N Sink)
  (return))
(finalize-module)
}

// Detect no base case for functions that visit each other.
// (Expect the error within the definition of ping as well.)
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+9 {{recursive visit has no base case (ie every branch continues a loop)}}
// expected-note@+12 {{recursive visit has no base case (ie every branch continues a loop)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_mutual)
(declare-match-fn pong)
; // Visit each function from the other in every branch.
(define-match-fn ping (N)
  (visit pong N))
(define-match-fn pong (N)
  (visit ping N))
(define-fn run_mutual ((N : 'int)) -> ()
  (visit ping N)
  (return))
(finalize-module)
}

// Require a function passed to a recursive visit to be the same in every
// iteration since a function is not a value that a loop can carry.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+13 {{recursive visit must pass the same function, member name, or closure as argument 1}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_changing_fn)
(define-match-fn finish (N)
  (visit '|nbdl::noop_t{}| N))
(define-match-fn finish_other (N)
  (visit '|nbdl::noop_t{}| N))
; // Pass a different function in the recursive visit.
(define-match-fn count_down_changing_fn (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down_changing_fn (visit '|std::minus<int>{}| N '|int{1}|)
                      finish_other)))
(define-fn run_changing_fn ((N : 'int)) -> ()
  (visit count_down_changing_fn N finish)
  (return))
(finalize-module)
}

// Require a closure passed to a recursive visit to be the same.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+9 {{recursive visit must pass the same function, member name, or closure as argument 1}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_changing_closure)
; // Pass a new closure in the recursive visit.
(define-match-fn count_down_changing_closure (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down_changing_closure (visit '|std::minus<int>{}| N '|int{1}|)
                      (match-fn (X) (visit Fn X)))))
(define-fn run_changing_closure ((N : 'int)) -> ()
  (visit count_down_changing_closure N (match-fn (X) (visit '|nbdl::noop_t{}| X)))
  (return))
(finalize-module)
}

// Require each visit on a closure to be in tail position.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+6 {{closure is not inlined (ie each visit on it must be in tail position)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_closure_not_tail)
(define-fn run_closure_not_tail ((N : 'int)) -> ()
  (match-if (visit (match-fn (X) (visit '|nbdl::noop_t{}| X)) N)
    (visit '|nbdl::noop_t{}| N)
    (visit '|nbdl::noop_t{}| N))
  (return))
(finalize-module)
}

// Check a recursive visit within a closure when the closure is inlined.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+12 {{recursive visit must be in tail position}}
// expected-note@+15 {{the function is visited here}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) begin))
(export-cpp run_in_closure)
(define-match-fn walk_in_closure (N Sink)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Sink N)
    (visit (match-fn (X)
             (begin
               (visit walk_in_closure X Sink)
               (visit Sink X)))
           (visit '|std::minus<int>{}| N '|int{1}|))))
(define-fn run_in_closure ((N : 'int) (Sink : 'nbdl::noop_t)) -> ()
  (visit walk_in_closure N Sink)
  (return))
(finalize-module)
}

// Check the base case of a recursive visit through a closure.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+8 {{recursive visit has no base case (ie every branch continues a loop)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_forever)
(define-match-fn forever (N)
  (visit (match-fn (X) (visit forever X)) N))
(define-fn run_forever ((N : 'int)) -> ()
  (visit forever N)
  (return))
(finalize-module)
}

// Re-enter a match-fn via a closure as a continue of its loop.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+11 {{'nbdl.continue' op type of loop-carried value 1 ('!nbdl.store<!nbdl.cpp<"nbdl::noop_t">>') does not match the type of the loop argument ('!nbdl.store<!nbdl.closure>')}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp run_reenter)
(define-match-fn count_down_reenter (N Fn)
  (match-if (visit '|std::equal_to<int>{}| N '|int{0}|)
    (visit Fn N)
    (visit count_down_reenter (visit '|std::minus<int>{}| N '|int{1}|) Fn)))
(define-fn run_reenter ((N : 'int)) -> ()
  (visit count_down_reenter N
         (match-fn (X) (visit count_down_reenter X '|nbdl::noop_t{}|)))
  (return))
(finalize-module)
}
