// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

//
// Copyright Jason Rice 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <string>

namespace {
namespace foo {

// A concrete callable type to receive results.
struct int_sink {
  int* dest;

  void operator()(int Value) const {
    *dest = Value;
  }
};

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) type))

(define i32 (type "i32"))
(define f32 (type "f32"))

(export-cpp point
            make_int
            make_point_y)

; // Define a struct where each key is a member name.
(define-struct point (X Y)
  (member: '.x 'int (init-args: X))
  (member: '.y 'int (init-args: Y))
  (member: '.name 'std::string (init-args: "point")))

; // Construct an object.
; // CHECK-LABEL: func.func @"::foo::make_int"
; // CHECK: [[INT:%[0-9]+]] = "nbdl.construct"({{%[0-9]+}})
; // CHECK-SAME: (i32) -> !nbdl.cpp<"int">
; // CHECK-NEXT: "nbdl.return"([[INT]])
(define-fn make_int () -> ((!cpp 'int))
  (return (construct 'int 5)))

; // Lift a constructed struct to a store to use it as the root of a path.
; // CHECK-LABEL: func.func @"::foo::make_point_y"
; // CHECK: [[POINT:%[0-9]+]] = "nbdl.construct"
; // CHECK-SAME: -> !nbdl.cpp<"foo::point">
; // CHECK-NEXT: [[STORE:%[0-9]+]] = "nbdl.lift_store"([[POINT]])
; // CHECK-SAME: -> !nbdl.store<!nbdl.cpp<"foo::point">>
; // CHECK: "nbdl.get"([[STORE]],
(define-fn make_point_y ((Sink : 'foo::int_sink)) -> ()
  (visit Sink (get (lift-store (construct point 3 4)) '.y))
  (return))

; // Define structs that are not exported to C++.
(define-struct vec2_s (X Y)
  (member: '.x i32 (init-args: X))
  (member: '.y f32 (init-args: Y)))

(define-struct segment_s (A B)
  (member: '.a vec2_s (init-args: A 1.0))
  (member: '.b vec2_s (init-args: B 2.0)))

; // Resolve the member types of structs without C++.
; // CHECK-LABEL: "nbdl.define_struct"() <{sym_name = "segment_s"}>
; // CHECK: "nbdl.construct"(%arg0, {{%[0-9]+}})
; // CHECK-SAME: -> !nbdl.struct<@vec2_s>
; // CHECK-LABEL: func.func @segment_b_y
; // CHECK: [[SEG:%[0-9]+]] = "nbdl.construct"(%arg0, %arg1)
; // CHECK-SAME: -> !nbdl.struct<@segment_s>
; // CHECK-NEXT: [[SEG_STORE:%[0-9]+]] = "nbdl.lift_store"([[SEG]])
; // CHECK: [[B:%[0-9]+]] = "nbdl.get"([[SEG_STORE]],
; // CHECK-SAME: -> !nbdl.store<!nbdl.struct<@vec2_s>>
; // CHECK: [[Y:%[0-9]+]] = "nbdl.get"([[B]],
; // CHECK-SAME: -> !nbdl.store<f32>
; // CHECK-NEXT: "nbdl.return"([[Y]])
(define-fn segment_b_y ((A : i32) (B : i32)) -> (f32)
  (match (get (lift-store (construct segment_s A B)) '.b '.y)
    (else => (lambda (Y) (return Y)))))

; // CHECK-LABEL: func.func @segment_a(%arg0: !nbdl.struct<@segment_s>)
; // CHECK-SAME: -> !nbdl.struct<@vec2_s>
; // CHECK: "nbdl.get"
; // CHECK-SAME: -> !nbdl.store<!nbdl.struct<@vec2_s>>
(define-fn segment_a ((S : segment_s)) -> (vec2_s)
  (match (get (lift-store S) '.a)
    (else => (lambda (A) (return A)))))

(write-nbdl-module (finalize-module))
}
}  // namespace foo
}  // namespace

int main() {
  auto P = foo::point(1, 2);
  SCHIR_ASSERT(P.x == 1);
  SCHIR_ASSERT(P.y == 2);
  SCHIR_ASSERT(P.name == std::string("point"));

  SCHIR_ASSERT(foo::make_int() == 5);

  int Y = 0;
  foo::make_point_y(foo::int_sink{&Y});
  SCHIR_ASSERT(Y == 4);
}
