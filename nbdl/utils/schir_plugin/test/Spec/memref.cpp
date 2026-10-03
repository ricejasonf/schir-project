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
#include <array>
#include <concepts>
#include <cstdint>
#include <mdspan>
#include <vector>

namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) attr create-op result load-dialect))

(export-c accumulate_i32 sum_i32 sum_i32_2d)
(export-cpp sum_vector)

(load-dialect "memref")
(define i32 (type "i32"))
(define index (type "index"))
(define !memref_i32 (type "memref<?xi32, strided<[?], offset: ?>>"))
(define !memref_2d_i32
  (type "memref<?x?xi32, strided<[?, ?], offset: ?>>"))

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

; // The element type is inferred from the memref, and the
; // element is unwrapped to call the lowered function.
; // CHECK-LABEL: func.func @"::foo::sum_i32"
; // CHECK-SAME: (%arg0: memref<?xi32, strided<[?], offset: ?>>,
; // CHECK-SAME: %arg1: memref<?xi32, strided<[?], offset: ?>>) {
; // CHECK: [[SRC:%[0-9]+]] = "nbdl.lift_store"(%arg0)
; // CHECK-NEXT: "nbdl.match_each"([[SRC]])
; // CHECK-NEXT: ^bb0([[X:%arg[0-9]+]]: !nbdl.store<i32>):
; // CHECK-NEXT: [[XVAL:%[0-9]+]] = "nbdl.unwrap"([[X]])
; // CHECK-SAME: (!nbdl.store<i32>) -> i32
; // CHECK-NEXT: func.call @"::foo::accumulate_i32"([[XVAL]], %arg1)
(define-fn sum_i32 ((Src : !memref_i32) (Dest : !memref_i32)) -> ()
  (match-each (lift-store Src)
    (lambda (X)
      (visit accumulate_i32 X Dest)))
  (return))

; // Elements of a memref with a rank > 1 are matched in row-major order.
; // TODO Actually check row-major order.
; // CHECK-LABEL: func.func @"::foo::sum_i32_2d"
; // CHECK: "nbdl.match_each"
; // CHECK-NEXT: ^bb0({{%arg[0-9]+}}: !nbdl.store<i32>):
(define-fn sum_i32_2d ((Src : !memref_2d_i32) (Dest : !memref_i32)) -> ()
  (match-each (lift-store Src)
    (lambda (X)
      (visit accumulate_i32 X Dest)))
  (return))

; // Bind each std::vector to a nbdl::memref and visit the lowered
; // function with the C++ arguments mapped to memrefs.
; // CHECK-LABEL: func.func @"::foo::sum_vector"
; // CHECK: [[FN:%[0-9]+]] = "nbdl.func_name"() <{name = @"::foo::sum_i32"}>
; // CHECK: [[SRC:%[0-9]+]] = "nbdl.visit"
; // CHECK-SAME: -> !nbdl.cpp<"nbdl::memref<int, 1, long>">
; // CHECK: [[DEST:%[0-9]+]] = "nbdl.visit"
; // CHECK-SAME: -> !nbdl.cpp<"nbdl::memref<int, 1, long>">
; // CHECK-NEXT: "nbdl.visit"([[FN]], [[SRC]], [[DEST]])
; // CHECK-SAME: <{validCppCrossMap}>
(define-match-fn sum_vector (Src Dest)
  (match-params ((Src : 'std::vector<int32_t> Src)
                 (Dest : 'std::vector<int32_t> Dest))
    (visit sum_i32 (visit 'nbdl::bind_memref Src)
                   (visit 'nbdl::bind_memref Dest))))

(finalize-module)
(write-nbdl-module)
} // schir_scheme
} // namespace foo
} // namespace

// The C++ wrapper of a lowered function takes nbdl::memref by value.
static_assert(std::same_as<decltype(foo::sum_i32),
                           void(nbdl::memref<std::int32_t, 1>,
                                nbdl::memref<std::int32_t, 1>)>);
static_assert(std::same_as<decltype(foo::sum_i32_2d),
                           void(nbdl::memref<std::int32_t, 2>,
                                nbdl::memref<std::int32_t, 1>)>);

int main() {
  std::vector<std::int32_t> src{1, 2, 3, 4, 5};
  std::vector<std::int32_t> dest{0};

  foo::sum_vector(src, dest);
  SCHIR_ASSERT(dest[0] == 15);

  // Call the lowered function via its C++ wrapper.
  dest[0] = 0;
  foo::sum_i32(nbdl::bind_memref(src), nbdl::bind_memref(dest));
  SCHIR_ASSERT(dest[0] == 15);

  // Respect the stride of the memref. (ie every other element)
  {
    using extents_t = std::dextents<std::intptr_t, 1>;
    std::layout_stride::mapping mapping(extents_t(3),
                                        std::array<std::intptr_t, 1>{2});
    std::mdspan strided(src.data(), mapping);
    dest[0] = 0;
    foo::sum_i32(nbdl::bind_memref(strided), nbdl::bind_memref(dest));
    SCHIR_ASSERT(dest[0] == 1 + 3 + 5);
  }

  // Match each element of a memref with a rank > 1.
  {
    std::array<std::int32_t, 6> data{1, 2, 3, 4, 5, 6};
    std::mdspan matrix(data.data(), std::dextents<std::intptr_t, 2>(2, 3));
    dest[0] = 0;
    foo::sum_i32_2d(nbdl::bind_memref(matrix), nbdl::bind_memref(dest));
    SCHIR_ASSERT(dest[0] == 21);
  }
}
