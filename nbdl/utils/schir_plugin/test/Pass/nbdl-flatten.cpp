// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <nbdl/spec.hpp>

namespace {
namespace foo {

// Has no match_impl so it is idempotent under
// the match operation.
struct not_a_store {
  int value = 5;
  float get_float() const { return 0.0f; }
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

(match-params-fn test_unit_match (Store Fn)
  (match (get Store '.value)
    (else => Fn)))

; // CHECK-LABEL @"::foo::test_infer_visit"
; // CHECK: [[MEMBER:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: "nbdl.visit"([[MEMBER]],
; // CHECK-SAME: : (!nbdl.member_name, !nbdl.store<!nbdl.cpp<"foo::not_a_store">>)
; //                     FIXME should have prefixed :: here --^
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"float">>
(match-params-fn test_infer_visit_result (Store Fn)
  (match (get Store '.value)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (visit Fn (visit '.get_float NotAStore))))))

(write-nbdl-module)

} // schir_scheme
