// Copyright Jason Rice 2026

#include <nbdl/spec.hpp>
#include <catch.hpp>
#include <array>
#include <vector>

namespace {
namespace foo {
template <size_t N>
using arr_vec = std::array<std::vector<int>, 3>;

#pragma schir_scheme
{
  (import (nbdl spec))

  (define-store context (Lol MessageReceiver)
    (store-compose '.lol
      (store '|arr_vec<3>|
             (init-args: Lol)))
    (store-compose '.receiver
      (store '|std::reference_wrapper<std::vector<int>>|
             (init-args: MessageReceiver))))

  (match-params-fn flatten (Context Fn)
    (match-each (get Context '.lol)
      (lambda (SubList)
        (match-each SubList
          (lambda (X)
            (visit '.push_back
                   (get Context '.receiver)
                   X)
            (Fn 42))))))
} // schir_scheme
} // namespace foo
} // namespace

TEST_CASE("Match elements of ranges.", "[spec][for-each]") {
  std::vector<int> receiver;
  foo::context context(
      foo::arr_vec<3>{{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}},
      std::ref(receiver));

  CHECK(receiver == std::vector<int>{});

  foo::flatten(context, nbdl::noop);
  CHECK(receiver == (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}));
}
