// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only %s > %t.out
// RUN: FileCheck %s < %t.out
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv

#include <nbdl/spec.hpp>
#include <nbdl/spec/shader.hpp>

namespace foo {

#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op load-dialect result))

(load-dialect "memref")
(export-shader add_f32 scale_points scale_soa add_pair soa_main)
(export-spirv-binary shaders)

(define f32 (type "f32"))
(define i32 (type "i32"))
(define index (type "index"))
(define !vec3 (type "vector<3xf32>"))
(define !buf (type "memref<?xf32>"))
(define !vbuf (type "memref<?xvector<3xf32>>"))

; // Define a struct of arrays with a binding for each member.
(define-struct soa_s (Weights Points)
  (member: '.weights !buf (init-args: Weights))
  (member: '.points !vbuf (init-args: Points)))

; // Define a struct of plain values.
(define-struct pair_s (A B)
  (member: '.a f32 (init-args: A))
  (member: '.b f32 (init-args: B)))

(define InvocationIndex (shader-global 'builtin "LocalInvocationIndex" i32))
(define Particles (shader-global 'readwrite-storage-buffer soa_s))

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

(define-fn add_f32 ((A : f32) (B : f32)) -> (f32)
  (return (result (create-op "arith.addf"
                             (loc: 0)
                             (operands: A B)
                             (attributes:)
                             (result-types: f32)))))

; // Store X as the first weight and copy the first point to the point at I.
(define-fn scale_points ((I : index) (X : f32) (W : !buf) (P : !vbuf)) -> ()
  (define V (result (create-op "memref.load"
                               (loc: 0)
                               (operands: P (index-zero))
                               (attributes:)
                               (result-types: !vec3))))
  (create-op "memref.store"
             (loc: 0)
             (operands: X W (index-zero))
             (attributes:)
             (result-types:))
  (create-op "memref.store"
             (loc: 0)
             (operands: V P I)
             (attributes:)
             (result-types:))
  (return))

; // Inline a function with a struct of memrefs at each call and erase it.
; // CHECK-LABEL: module @nbdl_spec_spirv_module
; // CHECK-NOT: spirv.func @"::foo::scale_soa"
(define-fn scale_soa ((I : index) (S : soa_s)) -> ()
  (match (lift-store S)
    (else => (lambda (S)
      (match-each (get S '.weights)
        (lambda (X)
          (visit scale_points I X (get S '.weights) (get S '.points)))))))
  (return))

; // Lower a struct of plain values to a spirv.struct.
; // CHECK: spirv.func @"::foo::add_pair"(%arg0: !spirv.struct<(f32, f32)>) -> f32
; // CHECK: spirv.CompositeExtract %arg0[0 : i32]
; // CHECK: spirv.CompositeExtract %arg0[1 : i32]
(define-fn add_pair ((P : pair_s)) -> (f32)
  (match (lift-store P)
    (else => (lambda (P)
      (return (visit add_f32 (get P '.a) (get P '.b)))))))

; // Give each member of a struct of storage buffers its own binding
; // and each runtime-sized member a length.
; // CHECK: spirv.GlobalVariable @soa_main_arg0 built_in("LocalInvocationIndex") : !spirv.ptr<i32, Input>
; // CHECK-NEXT: spirv.GlobalVariable @soa_main_arg1_0 bind(1, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @soa_main_arg1_1 bind(1, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<vector<3xf32>, stride=16> [0])>, StorageBuffer>
; // CHECK-NEXT: spirv.GlobalVariable @soa_main_lengths bind(2, 0) : !spirv.ptr<!spirv.struct<(i32 [0], i32 [4])>, Uniform>
; // CHECK-NEXT: spirv.func @soa_main() "None" {
; // CHECK-NOT: spirv.FunctionCall @"::foo::scale_soa"
; // CHECK-NOT: spirv.CompositeConstruct
; // CHECK: [[LENGTHS:%[^ ]+]] = spirv.mlir.addressof @soa_main_lengths
; // CHECK: spirv.AccessChain [[LENGTHS]][%cst0_i32{{[_0-9]*}}]
; // CHECK: spirv.mlir.loop
; // CHECK: spirv.Return
(define-shader-entrypoint soa_main ("Compute" 64 1 1)
    ((I : InvocationIndex) (S : Particles)) -> ()
  (visit scale_soa (index-cast I) S)
  (return))

(write-spirv-module)
(write-spirv-binary "shaders.spv")
(finalize-module)
}

// Count the members of a struct of storage buffers as storage buffers.
static_assert(shaders_entries.size() == 1);
static_assert(shaders_entries[0].num_storage_buffers == 0);
static_assert(shaders_entries[0].num_readwrite_storage_buffers == 2);
static_assert(shaders_entries[0].num_uniform_buffers == 1);
static_assert(shaders_entries[0].length_uniform_slot == 0);
static_assert(shaders_entries[0].length_readwrite_storage_buffers == 3);

}  // namespace foo
