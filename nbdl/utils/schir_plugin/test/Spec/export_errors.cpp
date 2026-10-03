// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_DEFINED_TWICE -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_EXPORTED_TO_BOTH -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Errors are reported at the name of the definition.
// expected-note@* {{error raised here}}

#ifdef TEST_DEFINED_TWICE
// expected-error@+7 {{exported name defined more than once: foo}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp foo)
(define-match-fn foo (Store Fn)
  (visit Fn Store))
(define-match-fn foo (Store Fn)
  (visit Fn Store))
}
#endif

#ifdef TEST_EXPORTED_TO_BOTH
// expected-error@+6 {{name cannot be exported to both C++ and C: foo}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp foo)
(export-c foo)
(define-match-fn foo (Store Fn)
  (visit Fn Store))
}
#endif
