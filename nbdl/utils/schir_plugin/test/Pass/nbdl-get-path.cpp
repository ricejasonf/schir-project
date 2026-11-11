// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <string>
#include <unordered_map>

namespace {
namespace foo {

struct not_a_store {
  int value = 5;
};

struct key_t { };
inline constexpr key_t key{};

using int_or_text = nbdl::variant<int, std::string, float>;
using int_to_text = std::unordered_map<int, std::string>;

struct app {
  not_a_store plain;
  int_or_text either;
  int_to_text names;
  int selected;
};

// Receive a path that is never called.
struct sink { };

// A store with a tag key.
struct keyed {
  not_a_store value;
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
(import (nbdl spec))

; // Key a member from a lifted root.
; // CHECK-LABEL: @test_get_path_member(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_get_path_member ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (get (lift-store S) '.plain))))

; // Key each overload of a single type so that the paths are unique.
; // CHECK-LABEL: @test_get_path_overloads(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">, !nbdl.match<!nbdl.cpp<"int">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">, !nbdl.match<!nbdl.cpp<"float">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">>
(define-fn test_get_path_overloads ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.either)
    ('int => (lambda (X) (visit Sink (get-path X))))
    ('float => (lambda (X) (visit Sink (get-path X))))
    (else => (lambda (X) (visit Sink (get-path X))))))

; // Key a nested path from the same root.
; // CHECK-LABEL: @test_get_path_nested(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">, !nbdl.path<!nbdl.member_name<"selected">>>
(define-fn test_get_path_nested ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match (lift-store S)
    ('foo::app =>
     (lambda (App)
       (visit Sink (get-path (get App '.names (get App '.selected))))))))

; // Key a tag.
; // CHECK-LABEL: @test_get_path_tag(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.tag<!nbdl.cpp<"foo::key_t">>>
(define-fn test_get_path_tag ((S : 'foo::keyed) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (get (lift-store S) (tag 'foo::key)))))

; // Leave the path of a parameter of a match-fn until it is inlined.
; // CHECK-LABEL: @test_get_path_callee(
; // CHECK: "nbdl.get_path"(%arg0)
; // CHECK-SAME: -> !nbdl.unknown
(define-match-fn test_get_path_callee (X Fn)
  (visit Fn (get-path X)))

; // Resolve the path of an inlined parameter from the root of the caller.
; // CHECK-LABEL: @test_get_path_inlined(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_get_path_inlined ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit test_get_path_callee (get (lift-store S) '.plain) Sink))

(write-nbdl-module (finalize-module))

} // schir_scheme
