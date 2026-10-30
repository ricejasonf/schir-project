// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -ferror-limit=0 -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-error@* 2 {{mlir pass pipeline failed}}
// expected-note@* {{error raised here}}

// expected-note@+11 {{struct cpp_member_s cannot be lowered since member x has type '!nbdl.cpp<"int">'}}
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) type))
(define i32 (type "i32"))

(export-c-internal cpp_member_param cpp_member_construct)
(define-struct cpp_member_s (X)
  (member: '.x 'int (init-args: X)))
(define-fn cpp_member_param ((S : cpp_member_s)) -> ()
  (return))
(define-fn cpp_member_construct ((X : i32)) -> ()
  (visit cpp_member_param (construct cpp_member_s X))
  (return))
(finalize-module)
}

// Lower a struct that is exported to C++.
// expected-note@+8 {{type requires C++ and cannot be lowered: '!nbdl.cpp<"visible_s">'}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp visible_s)
(export-c-internal visible_param)
(define-struct visible_s (X)
  (member: '.x i32 (init-args: X)))
(define-fn visible_param ((S : visible_s)) -> ()
  (return))
(finalize-module)
}

// Reject a memref with struct elements.
// expected-error@+7 {{mlir type parse failed}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-struct element_s (X)
  (member: '.x i32 (init-args: X)))
(define !memref_of_struct
  (type "memref<?x!nbdl.struct<@element_s>>"))
(finalize-module)
}

// expected-error@+8 {{storage buffer struct member is not a memref: plain_soa_s}}
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv))
(export-shader plain_soa_main)
(define-struct plain_soa_s (X)
  (member: '.x i32 (init-args: X)))
(define-shader-entrypoint plain_soa_main ("Compute" 1 1 1)
    ((S : (shader-global 'storage-buffer plain_soa_s))) -> ()
  (return))
(finalize-module)
}
