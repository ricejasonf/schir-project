// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics

namespace foo {
#pragma schir_scheme
{
(import (schir base)
        (schir clang))

(assert-equal (namespace-prefix) "::foo")
(assert-equal (namespace-prefix 'my_class) "::foo::my_class")

} // schir_scheme
} // namespace foo

namespace foo::bar {
#pragma schir_scheme
{
(import (schir base)
        (schir clang))

(assert-equal (namespace-prefix) "::foo::bar")
(assert-equal (namespace-prefix "my_class") "::foo::bar::my_class")

} // schir_scheme
} // namespace foo::bar

namespace foo { namespace baz {
#pragma schir_scheme
{
(import (schir base)
        (schir clang))

(assert-equal (namespace-prefix) "::foo::baz")
(assert-equal (namespace-prefix "my_class") "::foo::baz::my_class")

} // schir_scheme
}} // namespace foo::baz

namespace {
#pragma schir_scheme
{
(import (schir base)
        (schir clang))

(assert-equal (namespace-prefix) "::")
(assert-equal (namespace-prefix 'global_name) "::global_name")

} // schir_scheme
} // namespace

#pragma schir_scheme
{
(import (schir base)
        (schir clang))

(assert-equal (namespace-prefix) "::")
(assert-equal (namespace-prefix "global_name") "::global_name")

} // schir_scheme
