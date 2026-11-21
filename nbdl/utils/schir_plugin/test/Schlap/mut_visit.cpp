// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <string>
#include <unordered_map>

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
  std::unordered_map<int, int> items;
};

// Set the variant of an app or a node to text and count the calls.
struct set_text {
  int* calls;
  void operator()(app& A) const {
    A.either = std::string("text");
    ++*calls;
  }
  void operator()(node& N) const {
    N.value = std::string("text");
    ++*calls;
  }
};

// Count the calls without changing the app or the node.
struct peek {
  int* calls;
  void operator()(app const&) const { ++*calls; }
  void operator()(node const&) const { ++*calls; }
};

// Add up the ints it is given.
struct sink {
  int* dest;
  void operator()(int V) const { *dest += V; }
  void operator()(std::string const&) const {}
};

#pragma schir_scheme
{
(import (schir base) (nbdl spec) (nbdl spec impl) (nbdl spec schlap))

(export-cpp other_root plain_visit read_again same_root_path use_mutated
            sibling child_stable)

; // Accept a use of a store of another root after a mut visit.
(define-fn other_root ((S : 'foo::app) (T : 'foo::app)
                       (Set : 'foo::set_text) (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.either)
    ('int =>
     (lambda (I)
       (match (get (get (lift-store T) '.a) '.value)
         ('int => (lambda (J) (visit Set (mut! (lift-store T)))))
         (else => noop))
       (visit Sink I)))
    (else => noop))
  (return))

; // Accept a use after a plain visit of the same root.
(define-fn plain_visit ((S : 'foo::app) (Peek : 'foo::peek)
                        (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match (get (get Root '.a) '.value)
         ('int => (lambda (J) (visit Peek Root)))
         (else => noop))
       (visit Sink I)))
    (else => noop))
  (return))

; // Accept a store read from the root again after a mut visit.
(define-fn read_again ((S : 'foo::app) (Set : 'foo::set_text)
                       (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match (get (get Root '.a) '.value)
         ('int => (lambda (J) (visit Set (mut! Root))))
         (else => noop))
       (match (get Root '.either)
         ('int => (lambda (K) (visit Sink K)))
         (else => noop))))
    (else => noop))
  (return))

; // Accept a mut visit in a match-path on the root that its path came
; // from.
(define-fn same_root_path ((S : 'foo::app) (Set : 'foo::set_text)) -> ()
  (define Root (lift-store S))
  (match (get Root '.either)
    ('int =>
     (lambda (I)
       (match-path Root (get-path I)
         (lambda (X) (visit Set (mut! Root)))
         (lambda (X) noop))))
    (else => noop))
  (return))

; // Accept a use of a mut visited store and of its root after the visit.
(define-fn use_mutated ((S : 'foo::app) (Set : 'foo::set_text)
                        (Peek : 'foo::peek)) -> ()
  (match (lift-store S)
    ('foo::app =>
     (lambda (Root)
       (match (get Root '.a)
         ('foo::node =>
          (lambda (Node)
            (visit Set (mut! Node))
            (visit Peek Node)
            (visit Peek Root)))))))
  (return))

; // Accept a use of a value read from a sibling of a mut visited store.
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

; // Accept a use of a held element of a map after a child-stable insert
; // into the map.
(define-fn child_stable ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.items 1)
    ('int =>
     (lambda (Item)
       (visit '.emplace (child-stable-mut! (get Root '.items)) 2 20)
       (visit Sink Item)))
    (else => noop))
  (return))

(run-inference-passes)

; // Count the states of each check.
; // CHECK: (other_root 28)
; // CHECK-NEXT: (plain_visit 21)
; // CHECK-NEXT: (read_again 36)
; // CHECK-NEXT: (same_root_path 17)
; // CHECK-NEXT: (use_mutated 15)
; // CHECK-NEXT: (sibling 17)
; // CHECK-NEXT: (child_stable 22)
(for-each
  (lambda (Name Fn)
    (write (list Name (check-mut-visits Fn)))
    (newline))
  '(other_root plain_visit read_again same_root_path use_mutated sibling
    child_stable)
  (list other_root plain_visit read_again same_root_path use_mutated
        sibling child_stable))

(finalize-module)
}

} // namespace foo
} // namespace

int main() {
  {
    int Calls = 0, Sum = 0;
    foo::app S{.either = 1};
    foo::app T{.a = {.value = 2}, .either = 3};
    foo::other_root(S, T, foo::set_text{&Calls}, foo::sink{&Sum});
    SCHIR_ASSERT(Calls == 1 && Sum == 1);
  }
  {
    int Calls = 0, Sum = 0;
    foo::app S{.a = {.value = 2}, .either = 3};
    foo::plain_visit(S, foo::peek{&Calls}, foo::sink{&Sum});
    SCHIR_ASSERT(Calls == 1 && Sum == 3);
  }
  {
    int Calls = 0, Sum = 0;
    foo::app S{.a = {.value = 2}, .either = 3};
    foo::read_again(S, foo::set_text{&Calls}, foo::sink{&Sum});
    SCHIR_ASSERT(Calls == 1 && Sum == 0);
  }
  {
    int Calls = 0;
    foo::same_root_path(foo::app{.either = 4}, foo::set_text{&Calls});
    SCHIR_ASSERT(Calls == 1);
    foo::same_root_path(foo::app{.either = std::string("text")},
                        foo::set_text{&Calls});
    SCHIR_ASSERT(Calls == 1);
  }
  {
    int SetCalls = 0, PeekCalls = 0;
    foo::app S{.a = {.value = 2}};
    foo::use_mutated(S, foo::set_text{&SetCalls}, foo::peek{&PeekCalls});
    SCHIR_ASSERT(SetCalls == 1 && PeekCalls == 2);
  }
  {
    int Calls = 0, Sum = 0;
    foo::app S{.a = {.value = 2}, .either = 3};
    foo::sibling(S, foo::set_text{&Calls}, foo::sink{&Sum});
    SCHIR_ASSERT(Calls == 1 && Sum == 3);
  }
  {
    int Sum = 0;
    foo::app S{.items = {{1, 10}}};
    foo::child_stable(S, foo::sink{&Sum});
    SCHIR_ASSERT(Sum == 10);
  }
}
