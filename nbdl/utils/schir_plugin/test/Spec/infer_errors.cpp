// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_NOT_INFERRED -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_RETURN_MISMATCH -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Errors from the inference passes are raised by finalize-module
// with notes at the locations of the operations.
// expected-error@* {{nbdl inference passes failed}}

#ifdef TEST_NOT_INFERRED
// expected-note@+5 {{unable to infer result type of function: ::identity}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp identity)
(define-fn identity ((A : !nbdl.unknown)) -> (!nbdl.unknown)
  (return A))
(finalize-module)
}
#endif

#ifdef TEST_RETURN_MISMATCH
// expected-note@+7 {{type of return ('f32') does not match the type of a previous return ('i32')}}
// expected-note@+5 {{unable to infer result type of function: ::bad_returns}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp bad_returns)
(define-fn bad_returns ((A : 'bool)) -> (!nbdl.unknown)
  (match-if A (return 1) (return 2.5)))
(finalize-module)
}
#endif
