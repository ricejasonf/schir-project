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

// A store whose match without a key visits its value.
struct boxed {
  not_a_store value;
};

struct boxes {
  not_a_store plain;
  boxed box;
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

template <>
struct nbdl::match_impl<foo::boxed> {
  template <typename Store, typename Fn>
  static constexpr void apply(Store&& s, Fn&& fn) {
    std::forward<Fn>(fn)(std::forward<Store>(s).value);
  }
};

#pragma schir_scheme
{
(import (nbdl spec))

; // Give a root the unit key.
; // CHECK-LABEL: @test_get_path_root(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.unit>
(define-fn test_get_path_root ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (lift-store S))))

; // Key a member from a lifted root.
; // CHECK-LABEL: @test_get_path_member(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_get_path_member ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (get (lift-store S) '.plain))))

; // Key each alternative so that the paths are unique.
; // CHECK-LABEL: @test_get_path_overloads(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"nbdl::unresolved">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"int">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"std::{{basic_string<.*>|string}}">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"float">>>
(define-fn test_get_path_overloads ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.either)
    ('int => (lambda (X) (visit Sink (get-path X))))
    ('float => (lambda (X) (visit Sink (get-path X))))
    (else => (lambda (X) (visit Sink (get-path X))))))

; // Split an overload of several alternatives
; // that no path passes through.
; // CHECK-LABEL: @test_get_path_catch_all_split(
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">>):
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<!nbdl.cpp<"int">>):
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<
; // CHECK-SAME:   !nbdl.cpp<"std::{{basic_string<.*>|string}}">>):
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<!nbdl.cpp<"float">>):
; // CHECK-NOT: ^bb0
; // CHECK: func.func
(define-fn test_get_path_catch_all_split ((S : 'foo::app)
                                          (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.either)
    ('int => (lambda (X) (visit Sink X)))
    (else => (lambda (X) (visit Sink X)))))

; // Key the overload of a keyed match of several alternatives.
; // CHECK-LABEL: @test_get_path_keyed_match(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"nbdl::not_in_set">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"std::{{basic_string<.*>|string}}">>>
(define-fn test_get_path_keyed_match ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match (get Root '.names (get Root '.selected))
    ('std::string => (lambda (X) (visit Sink (get-path X))))
    (else => (lambda (X) (visit Sink (get-path X))))))

; // Do not key a match of a single alternative that is inlined.
; // CHECK-LABEL: @test_get_path_single_alt_inlined(
; // CHECK-NOT: "nbdl.match"
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_get_path_single_alt_inlined ((S : 'foo::boxes)
                                             (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.plain)
    ('foo::not_a_store => (lambda (X) (visit Sink (get-path X))))))

; // Do not key a match of a single alternative that is not inlined.
; // CHECK-LABEL: @test_get_path_single_alt_kept(
; // CHECK: "nbdl.match"
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"box">>
(define-fn test_get_path_single_alt_kept ((S : 'foo::boxes)
                                          (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.box)
    ('foo::not_a_store => (lambda (X) (visit Sink (get-path X))))))

; // Drop an unreachable overload and its path.
; // CHECK-LABEL: @test_get_path_unreachable(
; // CHECK: "nbdl.match"
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"box">>
; // CHECK-NOT: "nbdl.get_path"
; // CHECK: func.func
(define-fn test_get_path_unreachable ((S : 'foo::boxes)
                                      (Sink : 'foo::sink)) -> ()
  (match (get (lift-store S) '.box)
    ('foo::not_a_store => (lambda (X) (visit Sink (get-path X))))
    (else => (lambda (X) (visit Sink (get-path X))))))

; // Key a nested path from the same root and each alternative of its node.
; // CHECK-LABEL: @test_get_path_nested(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"nbdl::not_in_set">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"std::{{basic_string<.*>|string}}">>>
(define-fn test_get_path_nested ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match (lift-store S)
    ('foo::app =>
     (lambda (App)
       (visit Sink (get-path (get App '.names (get App '.selected))))))))

(define (GetSelected Root) (get Root '.selected))
(define (GetSelectedName Root) (get Root '.names (GetSelected Root)))

; // Build a path and its nested key with procedures that extend a path.
; // CHECK-LABEL: @test_get_path_procedures(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"nbdl::not_in_set">>>
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME:   !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME:   !nbdl.match<!nbdl.cpp<"std::{{basic_string<.*>|string}}">>>
(define-fn test_get_path_procedures ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (GetSelectedName (lift-store S)))))

; // Key a tag.
; // CHECK-LABEL: @test_get_path_tag(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.tag<!nbdl.cpp<"foo::key_t">>>
(define-fn test_get_path_tag ((S : 'foo::keyed) (Sink : 'foo::sink)) -> ()
  (visit Sink (get-path (get (lift-store S) (tag 'foo::key)))))

; // Leave the path of a parameter of a match-fn until it is inlined.
; // CHECK-LABEL: @test_get_path_callee(
; // CHECK: "nbdl.get_path"(%arg0)
; // CHECK-SAME: -> !nbdl.path{{$}}
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
