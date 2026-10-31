// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

// Copyright Jason Rice 2026

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <vector>

namespace {
namespace foo {

#pragma schir_scheme
{
(import (nbdl spec))

(export-cpp max_int find_greater)

; // Return from either branch of a match-if.
(define-fn max_int ((A : 'int) (B : 'int)) -> ('int)
  (match-if (visit '|std::greater<int>{}| A B)
    (return A)
    (return B)))

; // Return early from within a match-each and
; // otherwise return after it.
(define-fn find_greater ((V : '|std::vector<int>|) (X : 'int)) -> ('int)
  (match-each (lift-store V)
    (lambda (E)
      (match-params ((Elem : 'int E))
        (match-if (visit '|std::greater<int>{}| Elem X)
          (return Elem)))))
  (return X))

(finalize-module)
}

}  // namespace foo
}  // namespace

int main() {
  SCHIR_ASSERT(foo::max_int(2, 3) == 3);
  SCHIR_ASSERT(foo::max_int(5, -3) == 5);
  SCHIR_ASSERT(foo::find_greater({1, 5, 7, 9}, 4) == 5);
  SCHIR_ASSERT(foo::find_greater({1, 5, 7, 9}, 7) == 9);
  SCHIR_ASSERT(foo::find_greater({1, 2}, 4) == 4);
  SCHIR_ASSERT(foo::find_greater({}, 4) == 4);
}
