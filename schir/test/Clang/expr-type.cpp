// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics
#pragma schir_scheme
{
(import (schir base)
        (schir clang))
} // pragma schir_scheme

namespace mstd {
template <typename... T>
struct tuple { };
} // namespace mstd

namespace foo::bar {

struct foo { };
struct baz { };

template <typename T>
T default_construct() { return T{}; }

#pragma schir_scheme
{
  (assert-equal (expr->type "int{}") 'int)
  (assert-equal (expr->type "foo{}") 'foo::bar::foo)
  (assert-equal (expr->type "baz{}") 'foo::bar::baz)
  (assert-equal (expr->type "mstd::tuple<>{}")
                'mstd::tuple<>)
  (assert-equal (expr->type "mstd::tuple<mstd::tuple<>>{}")
                '|mstd::tuple<mstd::tuple<> >|)
  (assert-equal (expr->type "mstd::tuple<foo, baz, int>{}")
                '|mstd::tuple<foo::bar::foo, foo::bar::baz, int>|)
  (assert-equal (expr->type "mstd::tuple<foo, baz, mstd::tuple<int>>{}")
              '|mstd::tuple<foo::bar::foo, foo::bar::baz, mstd::tuple<int> >|)
} // pragma schir_scheme
} // namespace foo::bar
