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

// Receive a path or a value.
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
(import (schir base) (schir mlir) (nbdl spec) (nbdl spec context))

(define (GetPlain Root) (get Root '.plain))

; // Match a path from path-type and give the matched store the same path.
; // CHECK-LABEL: @test_match_path_member(
; // CHECK-NOT: "nbdl.tag"() : () -> !nbdl.path
; // CHECK: "nbdl.get"
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_match_path_member ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match-path Root (path-type (GetPlain Root))
    (lambda (X) (visit Sink (get-path X)))
    (lambda (X) (noop X))))

; // Expand a match key into an overload of its type and a copy
; // of the else (noop by default) that receives nbdl::not_matched.
; // CHECK-LABEL: @test_match_path_alt(
; // CHECK: "nbdl.match"
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<!nbdl.cpp<"int">>):
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"either">,
; // CHECK-SAME: !nbdl.match<!nbdl.cpp<"int">>>
; // CHECK: ^bb0(%{{.*}}: !nbdl.store<
; // CHECK-NEXT: %[[TAG:.*]] = "nbdl.tag"()
; // CHECK-SAME: -> !nbdl.tag<!nbdl.cpp<"nbdl::not_matched">>
; // CHECK-NEXT: "nbdl.noop"(%[[TAG]])
(define-fn test_match_path_alt ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match-path (lift-store S)
              (type "!nbdl.path<
                       !nbdl.member_name<\"either\">,
                       !nbdl.match<!nbdl.cpp<\"int\">>>")
    (lambda (X) (visit Sink (get-path X)))))

; // Match a nested path from the root as a key with a single copy
; // of the else for the match key.
; // CHECK-LABEL: @test_match_path_nested(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"names">,
; // CHECK-SAME: !nbdl.path<!nbdl.member_name<"selected">>,
; // CHECK-SAME: !nbdl.match<!nbdl.cpp<"std::basic_string<{{.*}}>">>>
; // CHECK: "nbdl.tag"() : () -> !nbdl.tag<!nbdl.cpp<"nbdl::not_matched">>
; // CHECK-NOT: "nbdl.tag"
; // CHECK: func.func
(define-fn test_match_path_nested ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (match-path (lift-store S)
              (type "!nbdl.path<
                       !nbdl.member_name<\"names\">,
                       !nbdl.path<!nbdl.member_name<\"selected\">>,
                       !nbdl.match<!nbdl.cpp_alias<\"std::string\">>>")
    (lambda (X) (visit Sink (get-path X)))
    (lambda (X) (noop X))))

; // Match a tag key.
; // CHECK-LABEL: @test_match_path_tag(
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.tag<!nbdl.cpp<"foo::key_t">>>
(define-fn test_match_path_tag ((S : 'foo::keyed) (Sink : 'foo::sink)) -> ()
  (define Root (lift-store S))
  (match-path Root (path-type (get Root (tag 'foo::key)))
    (lambda (X) (visit Sink (get-path X)))
    (lambda (X) (noop X))))

; // Leave a path match in a match-fn until it is inlined.
; // CHECK-LABEL: @test_match_path_callee(
; // CHECK: %[[KEY:.*]] = "nbdl.tag"()
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
; // CHECK-NEXT: "nbdl.match"(%arg0, %[[KEY]])
(define-match-fn test_match_path_callee (Root Fn)
  (match-path Root (type "!nbdl.path<!nbdl.member_name<\"plain\">>")
    (lambda (X) (visit Fn (get-path X)))
    (lambda (X) (noop X))))

; // Expand a path match from the root of the caller once it is inlined.
; // CHECK-LABEL: @test_match_path_inlined(
; // CHECK-NOT: "nbdl.tag"() : () -> !nbdl.path
; // CHECK: "nbdl.get_path"
; // CHECK-SAME: -> !nbdl.path<!nbdl.member_name<"plain">>
(define-fn test_match_path_inlined ((S : 'foo::app) (Sink : 'foo::sink)) -> ()
  (visit test_match_path_callee (lift-store S) Sink))

(write-nbdl-module (finalize-module))

} // schir_scheme
