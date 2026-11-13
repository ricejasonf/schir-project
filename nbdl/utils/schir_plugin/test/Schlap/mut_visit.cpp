// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -I %schlap_module_path -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
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

// Set the variant of an app to text and count the calls.
struct set_text {
  int* calls;
  void operator()(app& A) const {
    A.either = std::string("text");
    ++*calls;
  }
};

// Count the calls without changing the app.
struct peek {
  int* calls;
  void operator()(app const&) const { ++*calls; }
};

// Add up the ints it is given.
struct sink {
  int* dest;
  void operator()(int V) const { *dest += V; }
  void operator()(std::string const&) const {}
};

#pragma schir_scheme
{
(import (schir base) (nbdl spec) (nbdl spec schlap))

(export-cpp other_root plain_visit read_again same_root_path)

; // Accept a use of a store of another root after a mut visit.
(define-fn other_root ((S : 'foo::app) (T : 'foo::app)
                       (Set : 'foo::set_text) (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.either)
    ('int =>
     (lambda (I)
       (match (get (get (lift-store T) '.a) '.value)
         ('int => (lambda (J) (visit! Set (lift-store T))))
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
         ('int => (lambda (J) (visit! Set Root)))
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
         (lambda (X) (visit! Set Root))
         (lambda (X) noop))))
    (else => noop))
  (return))

(run-inference-passes)

; // Count the states of each check.
; // CHECK: (other_root 23)
; // CHECK-NEXT: (plain_visit 21)
; // CHECK-NEXT: (read_again 35)
; // CHECK-NEXT: (same_root_path 16)
(for-each
  (lambda (Name Fn)
    (write (list Name (check-mut-visits Fn)))
    (newline))
  '(other_root plain_visit read_again same_root_path)
  (list other_root plain_visit read_again same_root_path))

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
}
