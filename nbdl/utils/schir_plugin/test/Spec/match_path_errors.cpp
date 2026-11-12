// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_GET_STORE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_ROOT -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

namespace {
namespace foo {
struct inner {
  int value;
};

struct app {
  inner plain;
};

struct sink { };
} // namespace foo
} // namespace

#ifdef TEST_GET_STORE
// expected-note@* {{error raised here}}
// expected-error@+6 {{expecting a store}}
#pragma schir_scheme
{
(import (schir base) (schir mlir) (nbdl spec))
(define-fn get_store ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match-path (get Root '.plain) (get-path (get Root '.plain))
    (lambda (X) (visit Sink X))))
(finalize-module)
}
#endif

#ifdef TEST_NOT_ROOT
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+8 {{match-path expecting root store}}
#pragma schir_scheme
{
(import (schir base) (schir mlir) (nbdl spec))
(define-fn not_root ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.plain)
    ('foo::inner =>
     (lambda (Inner)
       (match-path Inner (get-path (get Inner '.value))
         (lambda (X) (visit Sink X)))))))
(finalize-module)
}
#endif
