// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>

#pragma schir_scheme
{
(import (nbdl spec))

; // No `else` case
(match-params-fn test_1 (fn)
  (match-if 'true
   (visit fn 42)))
} // schir_scheme

int main() {
  int result_1 = 0;
  test_1([&](int result) {
    result_1 = result;
  });
  SCHIR_ASSERT(result_1 == 42);
}
