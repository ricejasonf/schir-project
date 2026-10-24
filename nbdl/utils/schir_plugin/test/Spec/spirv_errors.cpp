// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-error@* 2 {{mlir pass pipeline failed}}
// expected-note@+14 {{dynamic memref size is not supported in SPIRV}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec spirv))

(export-shader store_first each_dynamic)

(define f32 (type "f32"))
(define !buf (type "memref<?xf32>"))

(define-fn store_first ((X : f32) (Dest : !buf)) -> ()
  (return))

(define-fn each_dynamic ((Src : !buf) (Dest : !buf)) -> ()
  (match-each (lift-store Src)
    (lambda (X)
      (visit store_first X Dest)))
  (return))

(finalize-module)
}

// expected-note@+7 {{unsupported shader storage buffer type: 'memref<?xf32, strided<[2]>, #spirv.storage_class<StorageBuffer>>'}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec spirv))
(export-shader entry_strided_buffer)
(define Strided
  (shader-global 'storage-buffer (type "memref<?xf32, strided<[2]>>")))
(define-shader-entrypoint entry_strided_buffer ("Compute" 1 1 1)
    ((S : Strided)) -> ()
  (return))
(finalize-module)
}
