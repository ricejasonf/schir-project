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

; // CHECK-LABEL: @"::test_infer_visit_result"
; // CHECK: [[MEMBER:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: "nbdl.visit"([[MEMBER]],
; // CHECK-SAME: : (!nbdl.member_name, !nbdl.store<!nbdl.cpp<"foo::not_a_store">>)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"float">>
(match-params-fn test_infer_visit_result (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (visit Fn (visit '.get_float NotAStore))))))

; // CHECK-LABEL: @"::test_infer_match_if_then_arg"
; // CHECK: [[MEMB0:%[0-9]+]] = "nbdl.member_name"() <{name = "value"}>
; // CHECK: [[GET0:%[0-9]+]] = "nbdl.get"(%arg{{[0-9]+}}, [[MEMB0]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK: "nbdl.match_if"([[GET0]])
; // CHECK-NEXT: ([[THENARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"int">>):
(match-params-fn test_infer_match_if_then_arg (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (match-cond
         ((get NotAStore '.value) => Fn)
         (else (visit Fn "nope"))
         )))))

; // CHECK-LABEL: @"::test_infer_match_if_then_arg_sfinae"
; // CHECK: [[MEMB0:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"([[MEMB0]], %arg{{[0-9]+}})
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"nbdl::detail::sfinae_result<float>">>
; // CHECK: "nbdl.match_if"([[VISIT0]])
; // CHECK-NEXT: ([[THENARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"float">>):
(match-params-fn test_infer_match_if_then_arg_sfinae (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (match-cond
         ((sfinae-visit '.get_float NotAStore) => Fn)
         (else (visit Fn "nope"))
         )))))

(write-nbdl-module)

} // schir_scheme
