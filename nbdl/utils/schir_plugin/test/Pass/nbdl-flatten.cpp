// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s

#include <nbdl/spec.hpp>

namespace {
namespace foo {

// Has no match_impl so it is idempotent under
// the match operation.
struct not_a_store {
  int value = 5;
};

// Match with unit key implemented to
// unwrap the contained value.
class weak_wrapper {
  not_a_store hidden_value = {42};

public:
  struct nbdl_match_impl {
    template <typename Self, typename Fn>
    static constexpr void apply(Self&& self, Fn&& fn) {
      std::forward<Fn>(fn)(std::forward<Self>(self).hidden_value);
    }
  };
};

} // namespace foo
} // namespace

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp
  test_0)

(match-params-fn test_unit_match (Store Fn)
  (match (get Store '.value)
    (else => Fn)))

; // TODO Test

; // FIXME To test this we need a way to specify the argument type.
(run-pass-nbdl-flatten)
(write-nbdl-module)

} // schir_scheme
