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
(import (nbdl spec))

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
