// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only %s > %t.out
// RUN: FileCheck %s < %t.out

#include <nbdl/spec.hpp>

namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) create-op result))

(export-shader scale fs_main vs_main cs_main)

(define !vec3 (type "vector<3xf32>"))
(define !vec4 (type "vector<4xf32>"))
(define !uvec3 (type "vector<3xi32>"))

; // Describe resources and builtins once and reuse them in entrypoints.
(define Scale (shader-global 'uniform-buffer !vec4))
(define Offset (shader-global 'uniform-buffer !vec4))
(define Position (shader-global 'builtin "Position" !vec4))
(define InvocationId (shader-global 'builtin "GlobalInvocationId" !uvec3))

(define-fn scale ((V : !vec4) (S : !vec4)) -> (!vec4)
  (return (result (create-op "arith.mulf"
                             (loc: 0)
                             (operands: V S)
                             (attributes:)
                             (result-types: !vec4)))))

(define-shader-entrypoint fs_main "Fragment" ((Color : !vec4)) -> (!vec4)
  (return Color))

(define-shader-entrypoint vs_main "Vertex"
    ((P : !vec4) (S : Scale) (O : Offset) (N : !vec3)) -> (Position !vec3)
  (return (visit scale P S) N))

(define-shader-entrypoint cs_main ("Compute" 64 1 1)
    ((I : InvocationId)) -> ()
  (return))

(write-spirv-module)
(finalize-module)

; // A define-fn exported to a shader remains a plain function.
; // CHECK-LABEL: module @nbdl_spec_spirv_module
; // CHECK: spirv.func @"::foo::scale"(%arg0: vector<4xf32>, %arg1: vector<4xf32>) -> vector<4xf32>

; // A fragment entrypoint has an Input and an Output at location 0.
; // CHECK: spirv.GlobalVariable @fs_main_arg0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
; // CHECK-NEXT: spirv.GlobalVariable @fs_main_result0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
; // CHECK-NEXT: spirv.func @fs_main() "None" {
; // CHECK-NEXT: [[ARG:%.*]] = spirv.mlir.addressof @fs_main_arg0
; // CHECK-NEXT: [[COLOR:%[0-9]+]] = spirv.Load "Input" [[ARG]] : vector<4xf32>
; // CHECK-NEXT: [[RESULT:%.*]] = spirv.mlir.addressof @fs_main_result0
; // CHECK-NEXT: spirv.Store "Output" [[RESULT]], [[COLOR]] : vector<4xf32>
; // CHECK-NEXT: spirv.Return

; // Plain types are numbered separately from uniform buffers in set 1
; // and builtins.
; // CHECK: spirv.GlobalVariable @vs_main_arg0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_arg1 bind(1, 0) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0])>, Uniform>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_arg2 bind(1, 1) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0])>, Uniform>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_arg3 {location = 1 : i32} : !spirv.ptr<vector<3xf32>, Input>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_result0 built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_result1 {location = 0 : i32} : !spirv.ptr<vector<3xf32>, Output>
; // CHECK-NEXT: spirv.func @vs_main() "None" {
; // CHECK: [[P:%[0-9]+]] = spirv.Load "Input"
; // CHECK: [[SCALE:%[0-9]+]] = spirv.AccessChain %vs_main_arg1_addr
; // CHECK-NEXT: [[S:%[0-9]+]] = spirv.Load "Uniform" [[SCALE]] : vector<4xf32>
; // CHECK: [[N:%[0-9]+]] = spirv.Load "Input" %vs_main_arg3_addr : vector<3xf32>
; // CHECK-NEXT: [[POS:%[0-9]+]] = spirv.FunctionCall @"::foo::scale"([[P]], [[S]])
; // CHECK-NEXT: %vs_main_result0_addr = spirv.mlir.addressof @vs_main_result0
; // CHECK-NEXT: spirv.Store "Output" %vs_main_result0_addr, [[POS]] : vector<4xf32>
; // CHECK-NEXT: %vs_main_result1_addr = spirv.mlir.addressof @vs_main_result1
; // CHECK-NEXT: spirv.Store "Output" %vs_main_result1_addr, [[N]] : vector<3xf32>
; // CHECK-NEXT: spirv.Return

; // CHECK: spirv.GlobalVariable @cs_main_arg0 built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
; // CHECK-NEXT: spirv.func @cs_main() "None" {

; // Only Input and Output variables are in the interface of an entry point.
; // CHECK: spirv.EntryPoint "Fragment" @fs_main, @fs_main_arg0, @fs_main_result0
; // CHECK-NEXT: spirv.ExecutionMode @fs_main "OriginUpperLeft"
; // CHECK-NEXT: spirv.EntryPoint "Vertex" @vs_main, @vs_main_arg0, @vs_main_arg3, @vs_main_result0, @vs_main_result1
; // CHECK-NEXT: spirv.EntryPoint "GLCompute" @cs_main, @cs_main_arg0
; // CHECK-NEXT: spirv.ExecutionMode @cs_main "LocalSize", 64, 1, 1
}

}  // namespace foo
