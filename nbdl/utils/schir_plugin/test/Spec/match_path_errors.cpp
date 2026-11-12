// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_GET_STORE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_ROOT -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_GET_PATH_TAG -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_PATH -fsyntax-only -Xclang -verify %s

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

struct key_t { };
inline constexpr key_t key{};
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
  (match-path (get Root '.plain)
              (type "!nbdl.path<!nbdl.member_name<\"value\">>")
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
       (match-path Inner (type "!nbdl.path<!nbdl.member_name<\"value\">>")
         (lambda (X) (visit Sink X)))))))
(finalize-module)
}
#endif

#ifdef TEST_GET_PATH_TAG
// expected-note@* {{error raised here}}
// expected-error@+7 {{path tag only valid with match-path}}
#pragma schir_scheme
{
(import (schir base) (schir mlir) (nbdl spec))
(define-fn get_path_tag ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root
              (type "!nbdl.path<!nbdl.member_name<\"plain\">>"))
    ('foo::inner => (lambda (X) (visit Sink X)))
    (else => (lambda (X) (noop X)))))
(finalize-module)
}
#endif

#ifdef TEST_NOT_PATH
// expected-note@* {{error raised here}}
// expected-error@+5 {{expecting path tag}}
#pragma schir_scheme
{
(import (schir base) (schir mlir) (nbdl spec))
(define-fn not_path_tag ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match-path (lift-store S) (tag 'foo::key)
    (lambda (X) (visit Sink X))
    (lambda (X) (noop X))))
(finalize-module)
}
#endif
