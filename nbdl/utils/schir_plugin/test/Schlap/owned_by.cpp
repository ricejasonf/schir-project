// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

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

// Set the variant of a node to text.
struct set_text {
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
#!curly-infix
(import (except (schir base) < > + - *)
        (schlap base) (schlap infix) (schlap rule) (nbdl spec))

(export-cpp twin sibling)

; // Invalidate the values that a mutated value may own.
(define-rule OwnedBy
  (variables invalid)
  (init {invalid = ∅})
  (on-mutate (v)
    {(′ invalid) = {invalid ∪ (owned-by v)}})
  (on-use (v)
    (require {v ∉ invalid})))

; // Write the message of the error that Thunk raises and of each note.
(define (write-error Thunk)
  (guard (E (#t (write (error-object-message E))
                (newline)
                (for-each (lambda (I)
                            (when (error-object? I)
                              (write (error-object-message I))
                              (newline)))
                          (error-object-irritants E))))
    (Thunk)
    (write "no error")
    (newline)))

; // Invalidate a value read from the twin of a mutated store.
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

; // Leave a value read from a sibling of a mutated store valid.
(define-fn sibling ((S : 'foo::app) (Set : 'foo::set_text)
                    (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (visit Set (mut! (get Root '.a)))
       (visit Sink I)))
    (else => noop))
  (return))

(run-inference-passes)

; // CHECK: "rule OwnedBy does not hold for function: ::foo::twin"
; // CHECK-NEXT: "invariant use of v9 by nbdl.visit #13 is violated after
; // CHECK: "step 4, nbdl.get #3 defines v5:
; // CHECK: "step 10, nbdl.get #10 defines v11:
; // CHECK-NEXT: "step 11, nbdl.visit #11 mutates v11 defines v12: invalid = {v7, v9}
(write-error
  (lambda () (check-rule* (source-loc) OwnedBy (named-fn-op twin))))
; // CHECK: "no error"
(write-error
  (lambda () (check-rule* (source-loc) OwnedBy (named-fn-op sibling))))
}

} // namespace foo
} // namespace
