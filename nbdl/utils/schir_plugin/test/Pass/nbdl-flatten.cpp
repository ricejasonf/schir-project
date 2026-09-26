// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <nbdl/spec.hpp>
#include <cstdint>
#include <string>
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

struct holder {
  not_a_store plain;
  weak_wrapper weak;
  std::string text;
  int32_t count;
};

} // namespace foo
} // namespace

#pragma schir_scheme
{
(import (nbdl spec))

(define-match-fn test_unit_match (Store Fn)
  (match (get Store '.value)
    (else => Fn)))

; // CHECK-LABEL: @"::test_infer_visit_result"
; // CHECK: [[MEMBER:%[0-9]+]] = "nbdl.member_name"() <{name = "get_float"}>
; // CHECK: "nbdl.visit"([[MEMBER]],
; // CHECK-SAME: : (!nbdl.member_name, !nbdl.store<!nbdl.cpp<"foo::not_a_store">>)
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"float">>
(define-match-fn test_infer_visit_result (Store Fn)
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
(define-match-fn test_infer_match_if_then_arg (Store Fn)
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
(define-match-fn test_infer_match_if_then_arg_sfinae (Store Fn)
  (match (get Store)
    ('foo::not_a_store =>
     (lambda (NotAStore)
       (match-cond
         ((sfinae-visit '.get_float NotAStore) => Fn)
         (else (visit Fn "nope"))
         )))))

; // CHECK-LABEL: @"::test_infer_match_each_element"
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

; // CHECK-LABEL: @"::test_inline_visit"
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

; // CHECK-LABEL: @"::test_inline_match"
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

; // CHECK-LABEL: @"::test_inline_match_else"
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
; // CHECK-LABEL: @"::test_no_inline_match_unit_impl"
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
; // CHECK-LABEL: @"::test_inline_match_non_cpp"
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
; // CHECK-LABEL: @"::test_inline_match_canonical"
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

; // CHECK-LABEL: @"::test_inline_match_canonical_alias"
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

; // Literal types should match corresponding c++ types of literals.
; // CHECK-LABEL: @"::test_inline_match_literal"
; // CHECK: [[LIT:%[0-9]+]] = "nbdl.literal"()
; // CHECK-SAME: -> !nbdl.store<i32>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[LIT]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_literal (Fn)
  (match 5
    ('int32_t => Fn)
    (else => noop)))

; // CHECK-LABEL: @"::test_inline_match_literal_float"
; // CHECK: [[LIT:%[0-9]+]] = "nbdl.literal"()
; // CHECK-SAME: -> !nbdl.store<f32>
; // CHECK-NOT: "nbdl.match"
; // CHECK: [[VISIT0:%[0-9]+]] = "nbdl.visit"(%arg{{[0-9]+}}, [[LIT]])
; // CHECK-NEXT: "nbdl.discard"([[VISIT0]])
; // CHECK-NEXT: }
(define-match-fn test_inline_match_literal_float (Fn)
  (match 3.14
    ('int32_t => noop)
    ('float => Fn)
    (else => noop)))

(write-nbdl-module)

} // schir_scheme
