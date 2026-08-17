// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

// Copyright Jason Rice 2026

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
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
    (match Context
      ('::foo::context =>
        (lambda (Context)
          (match-each (get Context '.lol)
            (lambda (SubList)
              (match-each SubList
                (lambda (X)
                  (visit '.push_back
                         (get Context '.receiver)
                         X)
                  (visit Fn 42)))))))))
} // schir_scheme
} // namespace foo
} // namespace

int main() {
  std::vector<int> receiver;
  foo::context context(
      foo::arr_vec<3>{{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}},
      std::ref(receiver));

  SCHIR_ASSERT(receiver == std::vector<int>{});

  foo::flatten(context, nbdl::noop);
  SCHIR_ASSERT(receiver == (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}));
}
