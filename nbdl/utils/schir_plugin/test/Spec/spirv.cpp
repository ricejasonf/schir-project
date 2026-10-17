// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

namespace {
namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) attr create-op result))

; // Export functions to C and a shader alongside each other.
(export-c add_i32 add_max_i32)
(export-c-internal max_i32)
(export-shader add_i32 max_i32 add_max_i32 scale_f32)

(define i32 (type "i32"))
(define f32 (type "f32"))

(define (constant Value T)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr Value T)))
                     (result-types: T))))

(define (binary-op Name A B T)
  (result (create-op Name
                     (loc: 0)
                     (operands: A B)
                     (attributes:)
                     (result-types: T))))

(define-fn add_i32 ((A : i32) (B : i32)) -> (i32)
  (return (binary-op "arith.addi" A B i32)))

(define-fn max_i32 ((A : i32) (B : i32)) -> (i32)
  (return (binary-op "arith.maxsi" A B i32)))

(define-fn add_max_i32 ((A : i32) (B : i32) (C : i32)) -> (i32)
  (return (visit add_i32 A (visit max_i32 B C))))

(define-fn scale_f32 ((X : f32) (S : f32)) -> (f32)
  (return (binary-op "arith.mulf" X S f32)))

(write-lower-module)
(write-spirv-module)
(finalize-module)

; // Lower each function once regardless of how many modules it is copied to.
; // CHECK-LABEL: module @nbdl_spec_lower_module
; // CHECK: func.func @"::foo::add_i32"(%arg0: i32, %arg1: i32) -> i32 {
; // CHECK-NEXT: arith.addi
; // CHECK-NOT: func.func @"::foo::add_i32"
; // CHECK: func.func @"::foo::scale_f32"
; // CHECK-NOT: func.func @"::foo::add_i32"

; // Copy the functions exported to a shader into the SPIRV module.
; // CHECK-LABEL: module @nbdl_spec_spirv_module
; // CHECK-NOT: func.func
; // CHECK: spirv.func @"::foo::add_i32"(%arg0: i32, %arg1: i32) -> i32
; // CHECK-NEXT: [[SUM:%[0-9]+]] = spirv.IAdd %arg0, %arg1 : i32
; // CHECK-NEXT: spirv.ReturnValue [[SUM]] : i32
; // CHECK: spirv.func @"::foo::max_i32"(%arg0: i32, %arg1: i32) -> i32
; // CHECK-NEXT: spirv.GL.SMax %arg0, %arg1 : i32
; // CHECK: spirv.func @"::foo::add_max_i32"
; // CHECK-SAME: (%arg0: i32, %arg1: i32, %arg2: i32) -> i32
; // CHECK-NEXT: [[MAX:%[0-9]+]] = spirv.FunctionCall @"::foo::max_i32"(%arg1, %arg2)
; // CHECK-NEXT: [[RESULT:%[0-9]+]] = spirv.FunctionCall @"::foo::add_i32"(%arg0, [[MAX]])
; // CHECK-NEXT: spirv.ReturnValue [[RESULT]] : i32
; // CHECK: spirv.func @"::foo::scale_f32"(%arg0: f32, %arg1: f32) -> f32
; // CHECK-NEXT: spirv.FMul %arg0, %arg1 : f32
; // CHECK-NOT: func.func
}

}  // namespace foo
}  // namespace

int main() {
  // Call the functions exported to both C and a shader.
  SCHIR_ASSERT(foo::add_i32(2, 3) == 5);
  SCHIR_ASSERT(foo::add_max_i32(1, 2, 3) == 4);
  SCHIR_ASSERT(foo::add_max_i32(1, 7, -3) == 8);
}
