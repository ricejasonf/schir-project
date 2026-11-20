// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <nbdl/spec.hpp>
#include <string>

namespace {
namespace foo {

using int_or_text = nbdl::variant<int, std::string, float>;

struct holder {
  int count;
};

} // namespace foo
} // namespace

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp test_cpp_visit test_cpp_visit_mut)

; // Create a visit with the mutArgs attribute.
; // CHECK-LABEL: @test_visit_mut(
; // CHECK: "nbdl.visit"(%arg1, %arg0) <{mutArgs = array<i32: 0>}>
(define-match-fn test_visit_mut (X Fn)
  (visit Fn (mut! X)))

; // List the index of each mut! operand.
; // CHECK-LABEL: @test_visit_mut_args(
; // CHECK: "nbdl.visit"(%arg3, %arg0, %arg1, %arg2)
; // CHECK-SAME: <{mutArgs = array<i32: 1, 2>}>
(define-match-fn test_visit_mut_args (X Y Z Fn)
  (visit Fn X (mut! Y) (mut! Z)))

(write-nbdl-module)

; // Translate a visit and a mut visit to the same C++.
; // CHECK-LABEL: inline void test_cpp_visit(auto&& arg_0, auto&& arg_1) {
; // CHECK-NEXT: ::nbdl::match(arg_0, [&]({{\[\[}}maybe_unused{{\]\]}} auto&& arg_2) {
; // CHECK-NEXT: auto&& get_3 = arg_2.count;
; // CHECK-NEXT: ::nbdl::match(get_3, [&]({{\[\[}}maybe_unused{{\]\]}} auto&& arg_4) {
; // CHECK-NEXT: arg_1(arg_4);
; // CHECK-LABEL: inline void test_cpp_visit_mut(auto&& arg_0, auto&& arg_1) {
; // CHECK-NEXT: ::nbdl::match(arg_0, [&]({{\[\[}}maybe_unused{{\]\]}} auto&& arg_2) {
; // CHECK-NEXT: auto&& get_3 = arg_2.count;
; // CHECK-NEXT: ::nbdl::match(get_3, [&]({{\[\[}}maybe_unused{{\]\]}} auto&& arg_4) {
; // CHECK-NEXT: arg_1(arg_4);
(define-match-fn test_cpp_visit (Store Fn)
  (visit Fn (get Store '.count)))

(define-match-fn test_cpp_visit_mut (Store Fn)
  (visit Fn (mut! (get Store '.count))))

(run-inference-passes)
(write-cpp "::test_cpp_visit")
(write-cpp "::test_cpp_visit_mut")

; // Keep the attribute of a visit inlined from a match-fn.
; // CHECK-LABEL: @test_visit_mut_inlined(
; // CHECK: ^bb0([[HOLDER:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::holder">>):
; // CHECK: [[COUNT:%[0-9]+]] = "nbdl.get"([[HOLDER]],
; // CHECK-NOT: "nbdl.visit"
; // CHECK: "nbdl.visit"(%arg{{[0-9]+}}, [[COUNT]]) <{mutArgs = array<i32: 0>}>
(define-match-fn test_visit_mut_inlined (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (visit test_visit_mut (get Holder '.count) Fn)))))

; // Keep the attribute of a visit inlined from a closure.
; // CHECK-LABEL: @test_visit_mut_closure(
; // CHECK: ^bb0([[HOLDER:%arg[0-9]+]]: !nbdl.store<!nbdl.cpp<"foo::holder">>):
; // CHECK: [[COUNT:%[0-9]+]] = "nbdl.get"([[HOLDER]],
; // CHECK-NOT: "nbdl.visit"
; // CHECK: "nbdl.visit"(%arg{{[0-9]+}}, [[COUNT]]) <{mutArgs = array<i32: 0>}>
(define-match-fn test_visit_mut_closure (Store Fn)
  (match (get Store)
    ('foo::holder =>
     (lambda (Holder)
       (visit (match-fn (X) (visit Fn (mut! X))) (get Holder '.count))))))

; // Keep the attribute in each copy of a catch-all overload.
; // CHECK-LABEL: @test_visit_mut_copied(
; // CHECK: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"nbdl::unresolved">>):
; // CHECK-NEXT: "nbdl.visit"({{.*}}) <{mutArgs = array<i32: 0>}>
; // CHECK: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"int">>):
; // CHECK-NEXT: "nbdl.visit"({{.*}}) <{mutArgs = array<i32: 0>}>
; // CHECK: ^bb0(%arg{{[0-9]+}}: !nbdl.store<
; // CHECK-SAME:   !nbdl.cpp<"std::{{basic_string<.*>|string}}">>):
; // CHECK-NEXT: "nbdl.visit"({{.*}}) <{mutArgs = array<i32: 0>}>
; // CHECK: ^bb0(%arg{{[0-9]+}}: !nbdl.store<!nbdl.cpp<"float">>):
; // CHECK-NEXT: "nbdl.visit"({{.*}}) <{mutArgs = array<i32: 0>}>
(define-match-fn test_visit_mut_copied (Store Fn)
  (match (get Store)
    ('foo::int_or_text =>
     (lambda (Variant)
       (match Variant
         (else => (lambda (X) (visit Fn (mut! X)))))))))

(write-nbdl-module (finalize-module))

} // schir_scheme
