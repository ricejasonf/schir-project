// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

using int_or_text = nbdl::variant<int, std::string, float>;
using int_to_text = std::unordered_map<int, std::string>;
using int_or_text_holder = nbdl::variant_holder<int, std::string, float>;
using int_or_text_alias = nbdl::strong_alias<int_or_text>;

// A composed store where compose_key matches the variant
// and any other key matches the parent map.
struct compose_key { };
using composed = nbdl::detail::store_composite_t<compose_key, int_or_text,
                                                 int_to_text>;

struct holder {
  not_a_store plain;
  weak_wrapper weak;
  std::string text;
  int32_t count;
};

// A store with a non member name key.
struct key_t { };
struct keyed {
  holder value;
};

} // namespace foo
} // namespace

template <>
struct nbdl::get_impl<foo::keyed> {
  template <typename Store>
  static constexpr decltype(auto) apply(Store&& s) {
    return std::forward<Store>(s);
  }

  template <typename Store>
  static constexpr decltype(auto) apply(Store&& s, foo::key_t) {
    return (std::forward<Store>(s).value);
  }
};

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) create-op result))

; // Get with a key that is not a member name uses nbdl::get.
; // CHECK-LABEL: @test_infer_get_key(
; // CHECK: ^bb0([[KEYED:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::keyed">>):
; // CHECK: ^bb0([[KEY:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::key_t">>):
; // CHECK: "nbdl.get"([[KEYED]], [[KEY]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::holder">>
(define-match-fn test_infer_get_key (Store Key Fn)
  (match (get Store)
    ('foo::keyed =>
     (lambda (Keyed)
       (match (get Key)
         ('foo::key_t =>
          (lambda (K)
            (visit Fn (result (create-op "nbdl.get"
                                         (loc: 0)
                                         (operands: Keyed K)
                                         (attributes:)
                                         (result-types: (type "!nbdl.store")))))))
         (else => noop))))
    (else => noop)))

; // Get with no key uses nbdl::get.
; // CHECK-LABEL: @test_infer_get_unit_key(
; // CHECK: ^bb0([[KEYED:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::keyed">>):
; // CHECK: "nbdl.get"([[KEYED]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::keyed">>
(define-match-fn test_infer_get_unit_key (Store Fn)
  (match (get Store)
    ('foo::keyed =>
     (lambda (Keyed)
       (visit Fn (result (create-op "nbdl.get"
                                    (loc: 0)
                                    (operands: Keyed)
                                    (attributes:)
                                    (result-types: (type "!nbdl.store")))))))
    (else => noop)))

(define-match-fn test_unit_match (Store Fn)
  (match (get Store '.value)
    (else => Fn)))

; // CHECK-LABEL: @test_infer_visit_result(
; // CHECK: [[MEMBER:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: "nbdl.visit"([[MEMBER]],
; // CHECK-SAME: : (!nbdl.member_name, !nbdl.store<!nbdl.cpp<"foo::not_a_store">>)
; // CHECK-SAME: -> !nbdl.cpp<"float">
(define-match-fn test_infer_visit_result (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (visit Fn (visit '.get_float NotAStore))))))

; // CHECK-LABEL: @test_infer_match_if_then_arg(
; // CHECK: [[MEMB0:%[0-9]+]] = "nbdl.member_name"() <{name = "value"}>
; // CHECK: [[GET0:%[0-9]+]] = "nbdl.get"(%arg{{[0-9]+}}, [[MEMB0]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK: "nbdl.match_if"([[GET0]])
; // CHECK-NEXT: ([[THENARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"int">>):
(define-match-fn test_infer_match_if_then_arg (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (match-cond
         ((get NotAStore '.value) => Fn)
         (else (visit Fn "nope"))
         )))))

; // CHECK-LABEL: @test_infer_match_if_then_arg_sfinae(
; // CHECK: [[MEMB0:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"([[MEMB0]], %arg{{[0-9]+}})
; // CHECK-SAME: -> !nbdl.cpp<"nbdl::detail::sfinae_result<float>">
; // CHECK: "nbdl.match_if"([[VISIT0]])
; // CHECK-NEXT: ([[THENARG:%arg[0-9]+]]: !nbdl.cpp<"float">):
(define-match-fn test_infer_match_if_then_arg_sfinae (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (match-cond
         ((sfinae-visit '.get_float NotAStore) => Fn)
         (else (visit Fn "nope"))
         )))))

; // CHECK-LABEL: @test_infer_match_each_element(
; // CHECK: "nbdl.match_each"({{[^)]+}})
; // CHECK-NEXT: ([[ARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"float">>)
(define-match-fn test_infer_match_each_element (Store Dest Fn)
  (match (get Store)
    ('std::vector<float> =>
     (lambda (Vector)
       (match-each Vector
         (lambda (Element)
          (visit '.push_back Dest Element))))))
  (visit Fn Dest))

(define-match-fn test_inline_callee (X Fn)
  (visit Fn X))

; // CHECK-LABEL: @test_inline_visit
; // CHECK-SAME: ([[STORE:%arg[0-9]+]]: !nbdl.store, [[FN:%arg[0-9]+]]: !nbdl.store)
; // CHECK: "nbdl.match"([[STORE]])
; // CHECK-NEXT: ^bb0([[ARG:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::not_a_store">>):
; // CHECK-NEXT: [[VISIT:%[0-9]+]] = "nbdl.visit"([[FN]], [[ARG]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT]])
(define-match-fn test_inline_visit (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (visit test_inline_callee NotAStore Fn)))))

; // CHECK-LABEL: @test_inline_match(
; // CHECK: "nbdl.match"
; // CHECK-NEXT: ^bb0([[HOLDER:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::holder">>):
; // CHECK-NEXT: [[MEMB0:%[0-9]+]] = "nbdl.member_name"() <{name = "plain"}>
; // CHECK-NEXT: [[GET0:%[0-9]+]] = "nbdl.get"([[HOLDER]], [[MEMB0]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::not_a_store">>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[GET0]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (match (get Holder '.plain)
         ('int => noop)
         ('foo::not_a_store => Fn)
         (else => noop))))))

; // CHECK-LABEL: @test_inline_match_else(
; // CHECK: "nbdl.match"
; // CHECK-NEXT: ^bb0([[HOLDER:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::holder">>):
; // CHECK: [[GET0:%[0-9]+]] = "nbdl.get"([[HOLDER]], {{%[0-9]+}})
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::not_a_store">>
; // CHECK: [[GET1:%[0-9]+]] = "nbdl.get"([[GET0]], {{%[0-9]+}})
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[GET1]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_else (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (match (get Holder '.plain '.value)
         ('float => noop)
         (else => Fn))))))

; // Matching weak_wrapper unwraps its value so it is not inlined.
; // CHECK-LABEL: @test_no_inline_match_unit_impl(
; // CHECK: [[GET0:%[0-9]+]] = "nbdl.get"
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::weak_wrapper">>
; // CHECK-NEXT: "nbdl.match"([[GET0]])
(define-match-fn test_no_inline_match_unit_impl (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (match (get Holder '.weak)
         (else => Fn))))))

; // Non-C++ types match themselves by default.
; // CHECK-LABEL: @test_inline_match_non_cpp(
; // CHECK: "nbdl.match"
; // CHECK-NEXT: ^bb0([[X:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[X]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_non_cpp (Store Fn)
  (match Store
    ((type "i32") =>
     (lambda (X)
       (match X
         ((type "f32") => noop)
         ((type "i32") => Fn)
         (else => noop))))))

; // Overload typenames are canonicalized.
; // CHECK-LABEL: @test_inline_match_canonical(
; // CHECK: [[TEXT:%[0-9]+]] = "nbdl.get"
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[TEXT]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_canonical (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (match (get Holder '.text)
         ('std::string => Fn)
         (else => noop))))))

; // CHECK-LABEL: @test_inline_match_canonical_alias(
; // CHECK: [[COUNT:%[0-9]+]] = "nbdl.get"
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"int">>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[COUNT]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_canonical_alias (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (match (get Holder '.count)
         ('int32_t => Fn)
         (else => noop))))))

; // Literal types should match corresponding c++ types of literals
; // when lifted to a store.
; // CHECK-LABEL: @test_inline_match_literal(
; // CHECK: [[LIT:%[0-9]+]] = "nbdl.literal"()
; // CHECK-SAME: -> i32
; // CHECK-NEXT: [[STORE:%[0-9]+]] = "nbdl.lift_store"([[LIT]])
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[STORE]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_literal (Fn)
  (match (lift-store 5)
    ('int32_t => Fn)
    (else => noop)))

; // CHECK-LABEL: @test_inline_match_literal_float(
; // CHECK: [[LIT:%[0-9]+]] = "nbdl.literal"()
; // CHECK-SAME: -> f32
; // CHECK-NEXT: [[STORE:%[0-9]+]] = "nbdl.lift_store"([[LIT]])
; // CHECK-SAME: -> !nbdl.store<f32>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[STORE]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_literal_float (Fn)
  (match (lift-store 3.14)
    ('int32_t => noop)
    ('float => Fn)
    (else => noop)))

; // The catch all overload of a match on a resolved store receives
; // every alternative probed from the C++ implementation of match.
; // CHECK-LABEL: @test_infer_match_single_else_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"nbdl::detail::variant<
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">,
; // CHECK-SAME: !nbdl.cpp<"int">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">,
; // CHECK-SAME: !nbdl.cpp<"float">>):
(define-match-fn test_infer_match_single_else_alts (Store Fn)
  (match (get Store)
    ('foo::int_or_text =>
     (lambda (Variant)
       (match Variant
         (else => Fn))))))

; // The catch all overload excludes the alternatives
; // handled by the previous overloads.
; // CHECK-LABEL: @test_infer_match_else_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"nbdl::detail::variant<
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"int">>):
; // CHECK: }, {
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">,
; // CHECK-SAME: !nbdl.cpp<"float">>):
(define-match-fn test_infer_match_else_alts (Store Fn)
  (match (get Store)
    ('foo::int_or_text =>
     (lambda (Variant)
       (match Variant
         ('int => noop)
         (else => Fn))))))

; // Matching std::unordered_map with a key yields the mapped
; // value or nbdl::not_in_set.
; // CHECK-LABEL: @test_infer_match_unordered_map_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"int">>):
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}, %arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::not_in_set">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">>):
(define-match-fn test_infer_match_unordered_map_alts (Store Key Fn)
  (match (get Store)
    ('foo::int_to_text =>
     (lambda (Map)
       (match (get Key)
         ('int =>
          (lambda (K)
            (match (get Map K)
              (else => Fn)))))))))

; // CHECK-LABEL: @test_infer_match_variant_holder_index_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"nbdl::variant_index_t">>):
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}, %arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"unsigned long">>):
(define-match-fn test_infer_match_variant_holder_index_alts (Store Key Fn)
  (match (get Store)
    ('foo::int_or_text_holder =>
     (lambda (Holder)
       (match (get Key)
         ('nbdl::variant_index_t =>
          (lambda (K)
            (match (get Holder K)
              (else => Fn)))))))))

; // variant_holder has no nbdl::unresolved alternative.
; // CHECK-LABEL: @test_infer_match_variant_holder_value_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"nbdl::variant_value_t">>):
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}, %arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"int">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">,
; // CHECK-SAME: !nbdl.cpp<"float">>):
(define-match-fn test_infer_match_variant_holder_value_alts (Store Key Fn)
  (match (get Store)
    ('foo::int_or_text_holder =>
     (lambda (Holder)
       (match (get Key)
         ('nbdl::variant_value_t =>
          (lambda (K)
            (match (get Holder K)
              (else => Fn)))))))))

; // A strong_alias of a store matches as the aliased store.
; // CHECK-LABEL: @test_infer_match_strong_alias_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"nbdl::strong_alias<
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">,
; // CHECK-SAME: !nbdl.cpp<"int">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">,
; // CHECK-SAME: !nbdl.cpp<"float">>):
(define-match-fn test_infer_match_strong_alias_alts (Store Fn)
  (match (get Store)
    ('foo::int_or_text_alias =>
     (lambda (Alias)
       (match Alias
         (else => Fn))))))

; // The composed key matches the composed value.
; // CHECK-LABEL: @test_infer_match_store_compose_key_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"foo::compose_key">>):
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}, %arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">,
; // CHECK-SAME: !nbdl.cpp<"int">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">,
; // CHECK-SAME: !nbdl.cpp<"float">>):
(define-match-fn test_infer_match_store_compose_key_alts (Store Key Fn)
  (match (get Store)
    ('foo::composed =>
     (lambda (Composed)
       (match (get Key)
         ('foo::compose_key =>
          (lambda (K)
            (match (get Composed K)
              (else => Fn)))))))))

; // Any other key matches the parent store.
; // CHECK-LABEL: @test_infer_match_store_compose_parent_alts(
; // CHECK: ^bb0({{.*}}!nbdl.cpp<"int">>):
; // CHECK-NEXT: "nbdl.match"(%arg{{[0-9]+}}, %arg{{[0-9]+}}) ({
; // CHECK-NEXT: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::not_in_set">,
; // CHECK-SAME: !nbdl.cpp<"std::basic_string<char, std::char_traits<char>, std::allocator<char> >">>):
(define-match-fn test_infer_match_store_compose_parent_alts (Store Key Fn)
  (match (get Store)
    ('foo::composed =>
     (lambda (Composed)
       (match (get Key)
         ('int =>
          (lambda (K)
            (match (get Composed K)
              (else => Fn)))))))))

(finalize-module)

(write-nbdl-module)

} // schir_scheme
