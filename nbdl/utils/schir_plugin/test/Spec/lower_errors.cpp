// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-error@* {{mlir pass pipeline failed}}
// expected-note@+20 {{expecting a return on every path of a function with results}}
// expected-note@+25 {{nbdl.return within a match_each or scope is not supported}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) type))

(export-c-internal id_i32 return_on_one_path first_i32)

(define i1 (type "i1"))
(define i32 (type "i32"))
(define !memref_i32 (type "memref<?xi32>"))

(define-fn id_i32 ((A : i32)) -> (i32)
  (return A))

; // Discard the result of the visit instead of returning it.
(define-fn return_on_one_path ((C : i1) (A : i32)) -> (i32)
  (match-if C
    (return A)
    (visit id_i32 A)))

; // Return from within the body of a loop over a memref.
(define-fn first_i32 ((Src : !memref_i32) (A : i32)) -> (i32)
  (match-each (lift-store Src)
    (lambda (X)
      (return X)))
  (return A))

(finalize-module)
}
