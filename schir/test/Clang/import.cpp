// RUN: clang++ -I %S/Inputs -I %schir_module_path -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s

#include <type_traits>

namespace foo {
// expected-error@+8{{expected unqualified-id}}
#pragma schir_scheme
{
;// The scheme environment is orthogonal to namespaces.
(import (my lib))
(import (schir base))

(lol-trait 'woof)
(lol-trait 'my-trait)
(define hello-foo "hello foo!")
(write hello-foo)
}
}

static_assert(std::is_empty_v<foo::woof>);

namespace bar {
#pragma schir_scheme
{
(write hello-foo)
(if (is-empty 'foo::woof)
  (lol-trait 'bark)
  (raise-error "expecting empty woof"))
}
}

static_assert(std::is_empty_v<bar::bark>);
static_assert(bar::bark::is_bark);
