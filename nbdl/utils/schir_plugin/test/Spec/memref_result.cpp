// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
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

(export-c-internal drop_front accumulate_i32)
(export-c sum_tail)
(export-cpp sum_vector_tail)

(load-dialect "memref")
(define i32 (type "i32"))
(define index (type "index"))
(define !memref_i32 (type "memref<?xi32, strided<[?], offset: ?>>"))

; // Return a view of Src without its first Count elements.
; // A memref result is only returned to other lowered functions.
; // CHECK-LABEL: func.func @drop_front
; // CHECK-SAME: -> memref<?xi32, strided<[?], offset: ?>>
; // CHECK: [[VIEW:%[a-z0-9]+]] = memref.subview
; // CHECK-NEXT: "nbdl.return"([[VIEW]])
(define-fn drop_front ((Src : !memref_i32) (Count : i32)) -> (!memref_i32)
  (define N
    (result (create-op "arith.index_cast"
                       (loc: 0)
                       (operands: Count)
                       (attributes:)
                       (result-types: index))))
  (define Zero
    (result (create-op "arith.constant"
                       (loc: 0)
                       (operands:)
                       (attributes: ("value" (attr "0" index)))
                       (result-types: index))))
  (define Size
    (result (create-op "memref.dim"
                       (loc: 0)
                       (operands: Src Zero)
                       (attributes:)
                       (result-types: index))))
  (define NewSize
    (result (create-op "arith.subi"
                       (loc: 0)
                       (operands: Size N)
                       (attributes:)
                       (result-types: index))))
  ; // The offset and size are dynamic (ie ShapedType::kDynamic).
  (return
    (result (create-op "memref.subview"
                       (loc: 0)
                       (operands: Src N NewSize)
                       (attributes:
                         ("operandSegmentSizes"
                          (attr "array<i32: 1, 1, 1, 0>"))
                         ("static_offsets"
                          (attr "array<i64: -9223372036854775808>"))
                         ("static_sizes"
                          (attr "array<i64: -9223372036854775808>"))
                         ("static_strides" (attr "array<i64: 1>")))
                       (result-types: !memref_i32)))))

; // Accumulate X into the first element of Dest.
(define-fn accumulate_i32 ((X : i32) (Dest : !memref_i32)) -> ()
  (define Zero
    (result (create-op "arith.constant"
                       (loc: 0)
                       (operands:)
                       (attributes: ("value" (attr "0" index)))
                       (result-types: index))))
  (define Prev
    (result (create-op "memref.load"
                       (loc: 0)
                       (operands: Dest Zero)
                       (attributes:)
                       (result-types: i32))))
  (define Sum
    (result (create-op "arith.addi"
                       (loc: 0)
                       (operands: Prev X)
                       (attributes:)
                       (result-types: i32))))
  (create-op "memref.store"
             (loc: 0)
             (operands: Sum Dest Zero)
             (attributes:)
             (result-types:))
  (return))

; // Use the returned view in a lowered function.
; // CHECK-LABEL: func.func @sum_tail
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @drop_front}>
; // CHECK-NEXT: [[TAIL:%[0-9]+]] = "nbdl.visit"([[FN]], %arg0, %arg1)
; // CHECK-SAME: -> memref<?xi32, strided<[?], offset: ?>>
; // CHECK-NEXT: "nbdl.lift_store"([[TAIL]])
(define-fn sum_tail ((Src : !memref_i32) (Count : i32) (Dest : !memref_i32))
                    -> ()
  (match-each (lift-store (visit drop_front Src Count))
    (lambda (X)
      (visit accumulate_i32 X Dest)))
  (return))

; // Bind each std::vector to a nbdl::memref to visit the lowered function.
; // CHECK-LABEL: func.func @"::foo::sum_vector_tail"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @sum_tail}>
; // CHECK: "nbdl.visit"([[FN]], {{%[0-9]+}}, {{%arg[0-9]+}}, {{%[0-9]+}})
; // CHECK-SAME: <{validCppCrossMap}>
(define-match-fn sum_vector_tail (Src Count Dest)
  (match-params ((Src : 'std::vector<int32_t> Src)
                 (Count : 'int32_t Count)
                 (Dest : 'std::vector<int32_t> Dest))
    (visit sum_tail (visit 'nbdl::bind_memref Src)
                    Count
                    (visit 'nbdl::bind_memref Dest))))

(finalize-module)
(write-nbdl-module)
} // schir_scheme
} // namespace foo
} // namespace

int main() {
  std::vector<std::int32_t> src{1, 2, 3, 4, 5};
  std::vector<std::int32_t> dest{0};
  foo::sum_vector_tail(src, 1, dest);
  SCHIR_ASSERT(dest[0] == 2 + 3 + 4 + 5);

  dest[0] = 0;
  foo::sum_vector_tail(src, 3, dest);
  SCHIR_ASSERT(dest[0] == 4 + 5);
}
