// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_SAME_ROOT -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SAME
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ENCLOSING_ARG -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ARG
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_MATCH_PATH -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PATH
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ENCLOSING_ARG -DTEST_UNCHECKED -fsyntax-only %s

#include <nbdl/spec.hpp>
#include <string>

namespace {
namespace foo {

using int_or_text = nbdl::variant<int, std::string>;

struct node {
  int_or_text value;
  int count = 0;
};

struct app {
  node a;
  int_or_text either;
};

// Set the variant of an app or a node to text.
struct set_text {
  void operator()(app& A) const { A.either = std::string("text"); }
  void operator()(node& N) const { N.value = std::string("text"); }
};

// Add up the ints it is given.
struct sink {
  int* dest;
  void operator()(int V) const { *dest += V; }
  void operator()(std::string const&) const {}
};

#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec schlap))

(export-cpp same_root enclosing_arg match_path_then_use)
}

#ifdef TEST_SAME_ROOT
#pragma schir_scheme
{
; // Report a use of a node read from the root after a mut visit of it in
; // a nested match.
; // SAME: mut_visit_errors.cpp:[[@LINE+17]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::same_root
; // SAME: [[@LINE+12]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v5 by nbdl.get #13 is violated after 11 steps
; // SAME: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // SAME: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 4, nbdl.get #3 defines v5: invalid = {}, pc = 4
; // SAME: [[@LINE+7]]:{{[0-9]+}}: note: (schir_scheme) step 9, nbdl.visit #9 mutates v5 defines v10: invalid = {v5, v7, v9}, pc = 10
(define-fn same_root ((S : 'foo::app) (Set : 'foo::set_text)
                      (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.a)
    ('foo::node =>
     (lambda (Node)
       (match (get Node '.value)
         ('int => (lambda (I) (visit! Set Node)))
         (else => noop))
       (visit Sink (get Node '.count)))))
  (return))

(run-inference-passes)
(check-mut-visits same_root)
}
#endif

#ifdef TEST_ENCLOSING_ARG
#pragma schir_scheme
{
; // Report a use of the argument of an enclosing overload after a mut visit
; // of its root in a nested match, which nbdl.scope allows.
; // ARG: mut_visit_errors.cpp:[[@LINE+23]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::enclosing_arg
; // ARG: [[@LINE+13]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v7 by nbdl.visit #16 is violated after 13 steps
; // ARG: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // ARG: [[@LINE+5]]:{{[0-9]+}}: note: (schir_scheme) step 5, nbdl.match #4 region 1 defines v7: invalid = {}, pc = 6
; // ARG: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 12, nbdl.visit #13 mutates v3 defines v14: invalid = {v11, v13, v5, v7, v9}, pc = 14
(define-fn enclosing_arg ((S : 'foo::app) (Set : 'foo::set_text)
                          (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match (get (get Root '.a) '.value)
         ('int => (lambda (J) (visit! Set Root)))
         (else => noop))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)
}
#ifndef TEST_UNCHECKED
#pragma schir_scheme
{
(check-mut-visits enclosing_arg)
}
#endif
#pragma schir_scheme
{
(finalize-module)
}
#endif

#ifdef TEST_MATCH_PATH
#pragma schir_scheme
{
; // Report a use of the argument whose path a same-root match-path takes
; // after a mut visit in the match-path.
; // PATH: mut_visit_errors.cpp:[[@LINE+20]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::match_path_then_use
; // PATH: [[@LINE+14]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v7 by nbdl.visit #12 is violated after 11 steps
; // PATH: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // PATH: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) step 5, nbdl.match #4 region 1 defines v7: invalid = {}, pc = 6
; // PATH: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 9, nbdl.match #9 region 0 defines v10: invalid = {}, pc = 10
; // PATH: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 10, nbdl.visit #10 mutates v3 defines v11: invalid = {v10, v5, v7, v9}, pc = 11
(define-fn match_path_then_use ((S : 'foo::app) (Set : 'foo::set_text)
                                (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match-path Root (get-path I)
         (lambda (X) (visit! Set Root))
         (lambda (X) noop))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits match_path_then_use)
}
#endif

} // namespace foo
} // namespace

