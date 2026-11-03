// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace {
namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir base) current-source-loc length list map
                           string-append string->symbol))

; // Build definition names at runtime.
(define (Name Suffix)
  (string->symbol (string-append "proc_" Suffix)))

(export-cpp* (Name "point") (Name "sum") (Name "second"))

; // Define a struct with a member per key in a runtime list.
; // CHECK-LABEL: "nbdl.define_struct"() <{sym_name = "::foo::proc_point"}>
; // CHECK-NEXT: ^bb0(%arg0: !nbdl.unknown, %arg1: !nbdl.unknown, %arg2: !nbdl.unknown)
; // CHECK-COUNT-3: "nbdl.store_compose"
(define Keys (list '.x '.y '.z))
(define Point
  (make-struct (current-source-loc) (Name "point") (length Keys)
    (lambda Params
      (map (lambda (Key Param)
             (list Key (store 'int (init-args: Param))))
           Keys Params))))

; // Define a function with parameter types from a runtime list.
; // CHECK-LABEL: func.func @"::foo::proc_sum"
; // CHECK-SAME: (%arg0: !nbdl.cpp<"int">, %arg1: !nbdl.cpp<"int">)
; // CHECK-SAME: -> !nbdl.cpp<"int">
(define Sum
  (make-fn (current-source-loc) (Name "sum") (list 'int 'int) (list 'int)
    (lambda (A B)
      (return (visit '|std::plus<int>{}| A B)))))

; // Define a match-fn with a runtime arity and its C++ wrapper.
; // CHECK-LABEL: nbdl.define_match_fn @"::foo::proc_second.match_fn"
; // CHECK-SAME: (%arg0: !nbdl.store, %arg1: !nbdl.store, %arg2: !nbdl.store)
; // CHECK-LABEL: func.func @"::foo::proc_second"
; // CHECK-SAME: (%arg0: !nbdl.unknown, %arg1: !nbdl.unknown, %arg2: !nbdl.unknown)
; // CHECK-COUNT-3: "nbdl.lift_store"
(define Second
  (make-match-fn (current-source-loc) (Name "second") 3
    (lambda (A B Fn)
      (visit Fn B))))

(write-nbdl-module (finalize-module))
}

}  // namespace foo
}  // namespace

int main() {
  auto P = foo::proc_point(1, 2, 3);
  SCHIR_ASSERT(P.x == 1);
  SCHIR_ASSERT(P.y == 2);
  SCHIR_ASSERT(P.z == 3);

  SCHIR_ASSERT(foo::proc_sum(2, 3) == 5);

  int Result = 0;
  foo::proc_second(1, 2, [&](int X) { Result = X; });
  SCHIR_ASSERT(Result == 2);
}
