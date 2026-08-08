// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>
#include <functional>
#include <string>

namespace {
namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec))

  (define (memref-copy Src Dest)
    (dump 'HERE)
    (noop Src))

  ; // Inst must be cpp_type or something that doesn't have match-impl
  (define-store my_context (Size)
   (store-compose '.buffer
    (store 'std::vector<int32_t> (init-args: Size))))

#;(match-params-fn test1 (Ctx Dest Fn)
   (define (Copy Memref1 Memref2)
     (memref-copy Memref1 Memref2))
   (match (get Ctx '.buffer)
     ((type "memref<4xi32>") =>
      (lambda (Memref)
       (match-each Memref
        (lambda (El)
         (memref-copy Memref Dest)))))))
  'end
} // schir_scheme
} // namespace foo
} // namespace

// Bind std::vector to memref.
int main() {
  // whoa
}
