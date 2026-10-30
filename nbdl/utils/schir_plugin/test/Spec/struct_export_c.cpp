// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t > %t.out
// RUN: FileCheck %s < %t.out
// RUN: %t

#include <nbdl/bind_memref.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <cstdint>
#include <vector>

namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) attr create-op result load-dialect))

(load-dialect "memref")
(export-c-internal add_f32 make_particle particle_pos particle_count
                   copy_elem copy_first)
(export-c particle_count_value particle_weight particle_y run_copy)

(define i32 (type "i32"))
(define f32 (type "f32"))
(define index (type "index"))
(define !vec3 (type "vector<3xf32>"))
(define !buf (type "memref<?xf32, strided<[?], offset: ?>>"))

; // Define structs that are lowered to tuples.
(define-struct count_s (N)
  (member: '.n i32 (init-args: N)))

(define-struct particle_s (Id Pos Weight)
  (member: '.id i32 (init-args: Id))
  (member: '.pos !vec3 (init-args: Pos))
  (member: '.weight f32 (init-args: Weight))
  (member: '.count count_s (init-args: Id))
  (member: '.zero f32 (init-args:)))

(define-struct buffers_s (Src Dest)
  (member: '.src !buf (init-args: Src))
  (member: '.dest !buf (init-args: Dest)))

(define-fn add_f32 ((A : f32) (B : f32)) -> (f32)
  (return (result (create-op "arith.addf"
                             (loc: 0)
                             (operands: A B)
                             (attributes:)
                             (result-types: f32)))))

; // Construct a struct from the members of its definition.
; // CHECK-LABEL: func.func @"::foo::make_particle"
; // CHECK-SAME: (%arg0: i32, %arg1: vector<3xf32>, %arg2: f32)
; // CHECK-SAME: -> !nbdl.tuple<i32, vector<3xf32>, f32, !nbdl.tuple<i32>, f32>
; // CHECK: [[COUNT:%[0-9]+]] = "nbdl.tuple_pack"(%arg0)
; // CHECK-SAME: (i32) -> !nbdl.tuple<i32>
; // CHECK-NEXT: [[ZERO:%[^ ]+]] = arith.constant 0.000000e+00 : f32
; // CHECK: [[P:%[0-9]+]] = "nbdl.tuple_pack"(%arg0, %arg1, %arg2, [[COUNT]], [[ZERO]])
; // CHECK-NEXT: return [[P]]
(define-fn make_particle ((Id : i32) (Pos : !vec3) (Weight : f32))
    -> (particle_s)
  (return (construct particle_s Id Pos Weight)))

; // Get a member of a struct passed between lowered functions.
; // CHECK-LABEL: func.func @"::foo::particle_pos"
; // CHECK-SAME: (%arg0: !nbdl.tuple<i32, vector<3xf32>, f32, !nbdl.tuple<i32>, f32>)
; // CHECK: [[POS:%[0-9]+]] = "nbdl.tuple_extract"(%arg0) <{index = 1 : i64}>
; // CHECK-NEXT: return [[POS]] : vector<3xf32>
(define-fn particle_pos ((P : particle_s)) -> (!vec3)
  (match (get (lift-store P) '.pos)
    (else => (lambda (Pos) (return Pos)))))

; // CHECK-LABEL: func.func @"::foo::particle_count"
; // CHECK: [[COUNT:%[0-9]+]] = "nbdl.tuple_extract"(%arg0) <{index = 3 : i64}>
; // CHECK-NEXT: [[N:%[0-9]+]] = "nbdl.tuple_extract"([[COUNT]]) <{index = 0 : i64}>
; // CHECK-NEXT: return [[N]] : i32
(define-fn particle_count ((P : particle_s)) -> (i32)
  (match (get (lift-store P) '.count '.n)
    (else => (lambda (N) (return N)))))

; // Pass only C++-mappable types across the C boundary.
(define-fn particle_count_value ((Id : i32) (Pos : !vec3) (Weight : f32))
    -> (i32)
  (return (visit particle_count (visit make_particle Id Pos Weight))))

(define-fn particle_weight ((Id : i32) (Pos : !vec3) (Weight : f32)) -> (f32)
  (match (lift-store (visit make_particle Id Pos Weight))
    (else => (lambda (P)
      (return (visit add_f32 (get P '.weight) (get P '.zero)))))))

(define-fn particle_y ((Id : i32) (Pos : !vec3) (Weight : f32)) -> (!vec3)
  (return (visit particle_pos (visit make_particle Id Pos Weight))))

; // Pass a struct of memrefs between lowered functions.
(define-fn copy_elem ((Src : !buf) (Dest : !buf)) -> ()
  (define Zero
    (result (create-op "arith.constant"
                       (loc: 0)
                       (operands:)
                       (attributes: ("value" (attr "0" index)))
                       (result-types: index))))
  (define X
    (result (create-op "memref.load"
                       (loc: 0)
                       (operands: Src Zero)
                       (attributes:)
                       (result-types: f32))))
  (create-op "memref.store"
             (loc: 0)
             (operands: X Dest Zero)
             (attributes:)
             (result-types:))
  (return))

; // CHECK-LABEL: func.func @"::foo::copy_first"
; // CHECK-SAME: (%arg0: !nbdl.tuple<memref<?xf32, strided<[?], offset: ?>>, memref<?xf32, strided<[?], offset: ?>>>)
(define-fn copy_first ((B : buffers_s)) -> ()
  (match (lift-store B)
    (else => (lambda (B)
      (visit copy_elem (get B '.src) (get B '.dest)))))
  (return))

(define-fn run_copy ((Src : !buf) (Dest : !buf)) -> ()
  (visit copy_first (construct buffers_s Src Dest))
  (return))

(write-lower-module)
(finalize-module)
} // schir_scheme
} // namespace foo
} // namespace

int main() {
  nbdl::vec_f32<3> Pos{1.0f, 2.0f, 3.0f};
  SCHIR_ASSERT(foo::particle_count_value(7, Pos, 2.5f) == 7);
  SCHIR_ASSERT(foo::particle_weight(7, Pos, 2.5f) == 2.5f);
  nbdl::vec_f32<3> Y = foo::particle_y(7, Pos, 2.5f);
  SCHIR_ASSERT(Y[0] == 1.0f && Y[1] == 2.0f && Y[2] == 3.0f);

  std::vector<float> Src{4.0f, 5.0f};
  std::vector<float> Dest{0.0f, 0.0f};
  foo::run_copy(nbdl::bind_memref(Src), nbdl::bind_memref(Dest));
  SCHIR_ASSERT(Dest[0] == 4.0f);
  SCHIR_ASSERT(Dest[1] == 0.0f);
}
