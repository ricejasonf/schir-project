// RUN: clang++ -std=c++26 \
// RUN:   -I %schir_module_path \
// RUN:   -I %nbdl_module_path \
// RUN:   -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fsyntax-only %s | FileCheck %s

#include <geomalg/nbdl.hpp>
#include <nbdl/spec/mlir.hpp>

namespace foo {
template <typename T>
struct bar { };
} // namespace foo

#pragma schir_scheme
{
(import (schir base)
        (schir clang)
        (nbdl spec)
        (nbdl spec geomalg))

; // Check type->cpp maps MlirStr to CppStr (canonicalized).
(define (check-type->cpp MlirStr CppStr)
  (define Result (type->cpp (type MlirStr)))
  (if (not (equal? Result (!cpp-type CppStr)))
    (error "type->cpp mismatch" MlirStr Result))
  (write Result)
  (newline))

; // Check the mapping in both directions where
; // nbdl::get_mlir_type is the inverse of type->cpp.
(define (check-bijective MlirStr CppStr)
  (define Result
    (type (expr-eval
            (string-append "::nbdl::detail::mlir_type_name<"
                           CppStr ">()"))))
  (if (not (equal? Result (type MlirStr)))
    (error "nbdl::get_mlir_type mismatch" CppStr Result))
  (check-type->cpp MlirStr CppStr))

(define (check-unsupported MlirStr)
  (define Result (type->cpp (type MlirStr)))
  (if Result
    (error "expecting unsupported type" MlirStr Result)))

; // Nbdl
; // CHECK: !nbdl.cpp<"foo::bar<int>">
(check-type->cpp "!nbdl.cpp<\"::foo::bar<int>\">" "::foo::bar<int>")
; // CHECK-NEXT: !nbdl.cpp<"std::basic_string_view<char, std::char_traits<char> >">
(check-bijective "!nbdl.string" "::std::string_view")

; // Builtin (Depends on nbdl)
; // CHECK-NEXT: !nbdl.cpp<"int">
(check-bijective "i32" "::std::int32_t")
; // CHECK-NEXT: !nbdl.cpp<"float">
(check-bijective "f32" "float")
; // CHECK-NEXT: !nbdl.cpp<"float __attribute__((ext_vector_type(3)))">
(check-bijective "vector<3xf32>" "::nbdl::vec_f32<3>")
; // CHECK-NEXT: !nbdl.cpp<"int __attribute__((ext_vector_type(4)))">
(check-bijective "vector<4xi32>" "::nbdl::vec_i32<4>")
; // CHECK-NEXT: !nbdl.cpp<"nbdl::memref<int, 1, long>">
(check-bijective "memref<?xi32, strided<[?], offset: ?>>"
                 "::nbdl::memref<::std::int32_t, 1>")
; // CHECK-NEXT: !nbdl.cpp<"nbdl::memref<float, 3, long>">
(check-bijective "memref<?x?x?xf32, strided<[?, ?, ?], offset: ?>>"
                 "::nbdl::memref<float, 3>")
; // CHECK-NEXT: !nbdl.cpp<"nbdl::memref<float __attribute__((ext_vector_type(4))), 2, long>">
(check-bijective "memref<?x?xvector<4xf32>, strided<[?, ?], offset: ?>>"
                 "::nbdl::memref<::nbdl::vec_f32<4>, 2>")

; // Geomalg
; // CHECK-NEXT: !nbdl.cpp<"geomalg::zero">
(check-bijective "!geomalg.zero" "::geomalg::zero")
; // CHECK-NEXT: !nbdl.cpp<"geomalg::blade<0>">
(check-bijective "!geomalg.blade<0>" "::geomalg::blade<0>")
; // CHECK-NEXT: !nbdl.cpp<"geomalg::blade<7>">
(check-bijective "!geomalg.blade<7>" "::geomalg::blade<7>")
; // CHECK-NEXT: !nbdl.cpp<"geomalg::multivector<geomalg::blade<1>, geomalg::blade<2>, geomalg::blade<4> >">
(check-bijective "!geomalg.multivector<<1>, <2>, <4>>"
                 "::geomalg::multivector<::geomalg::blade<1>, ::geomalg::blade<2>, ::geomalg::blade<4>>")
; // CHECK-NEXT: !nbdl.cpp<"geomalg::unit_vector<geomalg::blade<1>, geomalg::blade<2> >">
(check-bijective "!geomalg.unit_vector<<1>, <2>>"
                 "::geomalg::unit_vector<::geomalg::blade<1>, ::geomalg::blade<2>>")

; // Unsupported types
(check-unsupported "i64")
(check-unsupported "vector<3xi64>")
(check-unsupported "vector<2x3xf32>")
(check-unsupported "!nbdl.unit")
(check-unsupported "!geomalg.unknown")
; // Memrefs must be fully dynamic.
(check-unsupported "memref<?xi32>")
(check-unsupported "memref<4xi32, strided<[?], offset: ?>>")
(check-unsupported "memref<?xi32, strided<[1], offset: ?>>")
(check-unsupported "memref<?xi32, strided<[?], offset: 0>>")
(check-unsupported "memref<?xi64, strided<[?], offset: ?>>")
(check-unsupported "memref<i32, strided<[], offset: ?>>")
(check-unsupported "memref<?xi32, strided<[?], offset: ?>, 1>")
}
