// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only %s > %t.out
// RUN: FileCheck %s < %t.out
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv
// RUN: spirv-dis %t.dir/shaders.spv | FileCheck --check-prefix=SPIRV %s

// Only read-only storage buffers are NonWritable.
// SPIRV-DAG: OpDecorate %cs_main_arg1 NonWritable
// SPIRV-NOT: OpDecorate %cs_main_arg2 NonWritable
// SPIRV-DAG: OpDecorate %vs_main_arg1 NonWritable

#include <nbdl/spec.hpp>

namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op load-dialect result))

(load-dialect "memref")
(export-shader copy_at store_first sum_4 is_positive_f32 find_positive_4
               cs_main vs_main cs_sum)

(define f32 (type "f32"))
(define i32 (type "i32"))
(define index (type "index"))
(define !vec4 (type "vector<4xf32>"))
(define !buf (type "memref<?xf32>"))
(define !buf_4 (type "memref<4xf32>"))

(define InvocationIndex (shader-global 'builtin "LocalInvocationIndex" i32))
(define VertexIndex (shader-global 'builtin "VertexIndex" i32))
(define Position (shader-global 'builtin "Position" !vec4))
(define Src (shader-global 'storage-buffer !buf))
(define Dest (shader-global 'readwrite-storage-buffer !buf))
(define Scale (shader-global 'uniform-buffer f32))
(define Points (shader-global 'storage-buffer (type "memref<?xvector<4xf32>>")))
(define Weights (shader-global 'storage-buffer !buf_4))

(define (index-zero)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr "0" index)))
                     (result-types: index))))

(define (index-cast X)
  (result (create-op "arith.index_cast"
                     (loc: 0)
                     (operands: X)
                     (attributes:)
                     (result-types: index))))

; // A function receives a storage buffer as the pointer of its memref.
; // CHECK-LABEL: module @nbdl_spec_spirv_module
; // CHECK: spirv.func @"::foo::copy_at"(%arg0: i32,
; // CHECK-SAME: %arg1: !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>,
; // CHECK-SAME: %arg2: !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>)
; // CHECK: spirv.AccessChain %arg1
; // CHECK: spirv.Load "StorageBuffer"
; // CHECK: spirv.AccessChain %arg2
; // CHECK: spirv.Store "StorageBuffer"
(define-fn copy_at ((I : index) (S : !buf) (D : !buf)) -> ()
  (define X (result (create-op "memref.load"
                               (loc: 0)
                               (operands: S I)
                               (attributes:)
                               (result-types: f32))))
  (create-op "memref.store"
             (loc: 0)
             (operands: X D I)
             (attributes:)
             (result-types:))
  (return))

; // Store X as the first element of D.
(define-fn store_first ((X : f32) (D : !buf)) -> ()
  (create-op "memref.store"
             (loc: 0)
             (operands: X D (index-zero))
             (attributes:)
             (result-types:))
  (return))

; // A storage buffer with a static shape can be iterated.
; // CHECK-LABEL: spirv.func @"::foo::sum_4"
; // CHECK-SAME: !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0])>, StorageBuffer>
; // CHECK: spirv.mlir.loop
(define-fn sum_4 ((W : !buf_4) (D : !buf)) -> ()
  (match-each (lift-store W)
    (lambda (X)
      (visit store_first X D)))
  (return))

(define (f32-zero)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr "0.0" f32)))
                     (result-types: f32))))

(define-fn is_positive_f32 ((X : f32)) -> ((type "i1"))
  (return (result (create-op "arith.cmpf"
                             (loc: 0)
                             (operands: X (f32-zero))
                             (attributes:
                               ("predicate" (attr "2" (type "i64"))))
                             (result-types: (type "i1"))))))

; // Exit the loop over a storage buffer with the result of a return.
; // CHECK-LABEL: spirv.func @"::foo::find_positive_4"
; // CHECK-SAME: -> f32
; // CHECK: spirv.mlir.loop
; // CHECK: spirv.ReturnValue
(define-fn find_positive_4 ((W : !buf_4)) -> (f32)
  (match-each (lift-store W)
    (lambda (X)
      (match-if (visit is_positive_f32 X)
        (return X))))
  (return (f32-zero)))

; // Read-only storage buffers are in set 0, read-write storage buffers in
; // set 1, and uniform buffers in set 2 of a compute shader.
; // CHECK: spirv.GlobalVariable @cs_main_arg0 built_in("LocalInvocationIndex") : !spirv.ptr<i32, Input>
; // CHECK-NEXT: spirv.GlobalVariable @cs_main_arg1 bind(0, 0) {non_writable} : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @cs_main_arg2 bind(1, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @cs_main_arg3 bind(2, 0) : !spirv.ptr<!spirv.struct<(f32 [0])>, Uniform>
; // CHECK-NEXT: spirv.GlobalVariable @cs_main_arg4 bind(1, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>

; // Each runtime-sized storage buffer has a length in a uniform buffer that
; // follows the uniform buffers of the entry point.
; // CHECK-NEXT: spirv.GlobalVariable @cs_main_lengths bind(2, 1) : !spirv.ptr<!spirv.struct<(i32 [0], i32 [4], i32 [8])>, Uniform>
; // CHECK-NEXT: spirv.func @cs_main() "None" {

; // A call with a storage buffer is inlined since passing its pointer
; // requires variable pointers.
; // CHECK-NOT: spirv.FunctionCall
; // CHECK: [[SRC:%[^ ]+]] = spirv.mlir.addressof @cs_main_arg1
; // CHECK: [[DEST:%[^ ]+]] = spirv.mlir.addressof @cs_main_arg2
; // CHECK-NOT: spirv.FunctionCall
; // CHECK: spirv.AccessChain [[SRC]]
; // CHECK: spirv.Load "StorageBuffer"
; // CHECK: spirv.AccessChain [[DEST]]
; // CHECK: spirv.Store "StorageBuffer"
; // CHECK-NOT: spirv.FunctionCall
; // CHECK: spirv.Return
(define-shader-entrypoint cs_main ("Compute" 64 1 1)
    ((I : InvocationIndex) (S : Src) (D : Dest) (C : Scale) (D2 : Dest))
    -> ()
  (visit copy_at (index-cast I) S D)
  (return))

; // Storage buffers are in set 0 of a vertex shader.
; // CHECK: spirv.GlobalVariable @vs_main_arg1 bind(0, 0) {non_writable} : !spirv.ptr<!spirv.struct<(!spirv.rtarray<vector<4xf32>, stride=16> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_arg2 bind(0, 1) {non_writable} : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_result0
; // CHECK-NEXT: spirv.GlobalVariable @vs_main_lengths bind(1, 0) : !spirv.ptr<!spirv.struct<(i32 [0])>, Uniform>
(define-shader-entrypoint vs_main "Vertex"
    ((V : VertexIndex) (P : Points) (W : Weights)) -> (Position)
  (return (result (create-op "memref.load"
                             (loc: 0)
                             (operands: P (index-cast V))
                             (attributes:)
                             (result-types: !vec4)))))

; // A runtime-sized storage buffer can be iterated via its length.
; // CHECK: spirv.GlobalVariable @cs_sum_lengths bind(2, 1) : !spirv.ptr<!spirv.struct<(i32 [0], i32 [4])>, Uniform>
; // CHECK-LABEL: spirv.func @cs_sum()
; // CHECK-NOT: spirv.FunctionCall
; // CHECK: [[LENGTHS:%[^ ]+]] = spirv.mlir.addressof @cs_sum_lengths
; // CHECK: [[LENGTH_PTR:%[^ ]+]] = spirv.AccessChain [[LENGTHS]][%cst0_i32{{[_0-9]*}}]
; // CHECK: [[LENGTH:%[^ ]+]] = spirv.Load "Uniform" [[LENGTH_PTR]] : i32
; // CHECK: spirv.mlir.loop
; // CHECK: spirv.SLessThan %{{[0-9]+}}, [[LENGTH]] : i32
; // CHECK-NOT: spirv.FunctionCall
; // CHECK: spirv.Return
(define-shader-entrypoint cs_sum ("Compute" 1 1 1)
    ((C : Scale) (S : Src) (W : Weights) (D : Dest)) -> ()
  (match-each (lift-store S)
    (lambda (X)
      (visit store_first X D)))
  (return))

(write-spirv-module)
(write-spirv-binary "shaders.spv")
(finalize-module)
}

}  // namespace foo
