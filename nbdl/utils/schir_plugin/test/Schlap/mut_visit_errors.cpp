// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_OWNED -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OWNED
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_TWIN -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TWIN
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_OPERAND -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OPERAND
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ENCLOSING_ARG -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ARG
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_MATCH_PATH -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PATH
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_HELD_ELEMENT -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HELD
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ASSIGN -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ASSIGN
// RUN: not clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ASSIGN_SRC -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SRC
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -DTEST_ENCLOSING_ARG -DTEST_UNCHECKED -fsyntax-only %s

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <string>
#include <unordered_map>

namespace {
namespace foo {

using int_or_text = nbdl::variant<int, std::string>;
using int_or_text_holder = nbdl::variant_holder<int, std::string>;

struct node {
  int_or_text value;
  int count = 0;
};

struct app {
  node a;
  int_or_text either;
  std::unordered_map<int, int> items;
  int_or_text_holder held;
  node b;
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

// Set the variant of an app to the count of a node.
struct set_count {
  void operator()(app& A, node const& N) const { A.either = N.count; }
};

#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec schlap))

(export-cpp owned_value twin operand enclosing_arg match_path_then_use
            held_element assign_owned assign_src)
}

#ifdef TEST_OWNED
#pragma schir_scheme
{
; // Report a use of a value read from a node before a mut visit of the
; // node, and accept a use of a value read from it again.
; // OWNED: mut_visit_errors.cpp:[[@LINE+17]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::owned_value
; // OWNED: [[@LINE+10]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v9 by nbdl.visit #12 is violated after 12 steps
; // OWNED: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // OWNED: [[@LINE+7]]:{{[0-9]+}}: note: (schir_scheme) step 8, nbdl.match #7 region 1 defines v9: invalid = {}, pc = 9
; // OWNED: [[@LINE+7]]:{{[0-9]+}}: note: (schir_scheme) step 10, nbdl.visit #10 mutates v5: invalid = {v7, v9}, pc = 11
(define-fn owned_value ((S : 'foo::app) (Set : 'foo::set_text)
                        (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.a)
    ('foo::node =>
     (lambda (Node)
       (match (get Node '.value)
         ('int => (lambda (I) (visit Set (mut! Node)) (visit Sink I)))
         (else => noop))
       (visit Sink (get Node '.count)))))
  (return))

(run-inference-passes)
(check-mut-visits owned_value)
}
#endif

#ifdef TEST_TWIN
#pragma schir_scheme
{
; // Report a use of a value read from the twin of a store after a mut
; // visit of the store.
; // TWIN: mut_visit_errors.cpp:[[@LINE+18]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::twin
; // TWIN: [[@LINE+12]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v9 by nbdl.visit #13 is violated after 13 steps
; // TWIN: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // TWIN: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) step 7, nbdl.match #6 region 1 defines v9: invalid = {}, pc = 8
; // TWIN: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 10, nbdl.get #10 defines v11: invalid = {}, pc = 11
; // TWIN: [[@LINE+7]]:{{[0-9]+}}: note: (schir_scheme) step 11, nbdl.visit #11 mutates v11: invalid = {v7, v9}, pc = 12
(define-fn twin ((S : 'foo::app) (Set : 'foo::set_text)
                 (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get (get Root '.a) '.value)
    ('int =>
     (lambda (I)
       (visit Set (mut! (get Root '.a)))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits twin)
}
#endif

#ifdef TEST_OPERAND
#pragma schir_scheme
{
; // Report an operand of a visit that is read from a mut operand of the
; // same visit.
; // OPERAND: mut_visit_errors.cpp:[[@LINE+11]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::operand
; // OPERAND: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v4 by nbdl.visit #4 is violated after 5 steps
; // OPERAND: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // OPERAND: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 4, nbdl.get #3 defines v4: invalid = {}, pc = 4
; // OPERAND: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) step 5, nbdl.visit #4 mutates v2: invalid = {v4}, pc = 5
(define-fn operand ((S : 'foo::app) (Set : 'foo::set_count)) -> ()
  (define Root (lift-store S))
  (visit Set (mut! Root) (get Root '.a))
  (return))

(run-inference-passes)
(check-mut-visits operand)
}
#endif

#ifdef TEST_ENCLOSING_ARG
#pragma schir_scheme
{
; // Report a use of the argument of an enclosing overload after a mut visit
; // of its root in a nested match, which nbdl.scope allows.
; // ARG: mut_visit_errors.cpp:[[@LINE+23]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::enclosing_arg
; // ARG: [[@LINE+13]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v7 by nbdl.visit #16 is violated after 14 steps
; // ARG: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // ARG: [[@LINE+5]]:{{[0-9]+}}: note: (schir_scheme) step 5, nbdl.match #4 region 1 defines v7: invalid = {}, pc = 6
; // ARG: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 12, nbdl.visit #13 mutates v3: invalid = {v11, v13, v5, v7, v9}, pc = 14
(define-fn enclosing_arg ((S : 'foo::app) (Set : 'foo::set_text)
                          (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match (get (get Root '.a) '.value)
         ('int => (lambda (J) (visit Set (mut! Root))))
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
; // PATH: [[@LINE+14]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v7 by nbdl.visit #12 is violated after 12 steps
; // PATH: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // PATH: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) step 5, nbdl.match #4 region 1 defines v7: invalid = {}, pc = 6
; // PATH: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 9, nbdl.match #9 region 0 defines v10: invalid = {}, pc = 10
; // PATH: [[@LINE+8]]:{{[0-9]+}}: note: (schir_scheme) step 10, nbdl.visit #10 mutates v3: invalid = {v10, v5, v7, v9}, pc = 11
(define-fn match_path_then_use ((S : 'foo::app) (Set : 'foo::set_text)
                                (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match-path Root (get-path I)
         (lambda (X) (visit Set (mut! Root)))
         (lambda (X) noop))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits match_path_then_use)
}
#endif

#ifdef TEST_HELD_ELEMENT
#pragma schir_scheme
{
; // Report a use of a held element of a map after a mut visit that inserts
; // into the map.
; // HELD: mut_visit_errors.cpp:[[@LINE+16]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::held_element
; // HELD: [[@LINE+10]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v8 by nbdl.visit #16 is violated after 17 steps
; // HELD: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // HELD: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 8, nbdl.match #7 region 0 defines v8: invalid = {}, pc = 8
; // HELD: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) step 15, nbdl.visit #14 mutates v11: invalid = {v6, v7, v8}, pc = 15
(define-fn held_element ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.items 1)
    ('int =>
     (lambda (Item)
       (visit '.emplace (mut! (get Root '.items)) 2 20)
       (visit Sink Item)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits held_element)
}
#endif

#ifdef TEST_ASSIGN
#pragma schir_scheme
{
; // Report an assign to a variant holder from its own alternative.
; // ASSIGN: mut_visit_errors.cpp:[[@LINE+13]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::assign_owned
; // ASSIGN: [[@LINE+7]]:{{[0-9]+}}: note: (schir_scheme) invariant mutation of v7 by nbdl.assign #10 is violated after 11 steps
; // ASSIGN: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // ASSIGN: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 8, nbdl.match #7 region 0 defines v7: invalid = {}, pc = 8
; // ASSIGN: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 11, nbdl.assign #10 mutates v9: invalid = {v5, v6, v7}, pc = 11
(define-fn assign_owned ((S : 'foo::app)) -> ()
  (define Root (lift-store S))
  (match (get Root '.held 'nbdl::variant_value)
    ('int => (lambda (I) (assign! (get Root '.held) I)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits assign_owned)
}
#endif

#ifdef TEST_ASSIGN_SRC
#pragma schir_scheme
{
; // Report a use of a value read from the source of an assign after the
; // assign.
; // SRC: mut_visit_errors.cpp:[[@LINE+16]]:{{[0-9]+}}: error: (schir_scheme) rule MutVisit does not hold for function: ::foo::assign_src
; // SRC: [[@LINE+10]]:{{[0-9]+}}: note: (schir_scheme) invariant use of v8 by nbdl.visit #16 is violated after 17 steps
; // SRC: [[@LINE+3]]:{{[0-9]+}}: note: (schir_scheme) initial state: invalid = {}, pc = 0
; // SRC: [[@LINE+4]]:{{[0-9]+}}: note: (schir_scheme) step 7, nbdl.match #6 region 1 defines v8: invalid = {}, pc = 8
; // SRC: [[@LINE+6]]:{{[0-9]+}}: note: (schir_scheme) step 14, nbdl.assign #13 mutates v12: invalid = {v6, v8}, pc = 15
(define-fn assign_src ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get (get Root '.a) '.value)
    ('int =>
     (lambda (I)
       (assign! (get Root '.b) (get Root '.a))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)
(check-mut-visits assign_src)
}
#endif

} // namespace foo
} // namespace

