// RUN: clang++ -std=c++26 -I %schir_module_path -I %geomalg_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <schir/SCHIR_ASSERT.h>

extern "C"
float test_add_func(float, float, float);

#pragma schir_scheme
{
(import (schir base)
        (schir llvm pass)
        (schir mlir all-passes)
        (geomalg base))

(define-func test_add_func ((a : e1) (b : e1) (c : e1))
  (sum a b c))
(run-passes geomalg-current-module 
          "geomalg-to-llvm")
(inject-module geomalg-current-module)
}

int main() {
  SCHIR_ASSERT(test_add_func(128, 64, 64) == 256);
}
