// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Errors are reported at the name of the definition.
// expected-note@* 4 {{error raised here}}

// expected-error@+7 {{exported name defined more than once: defined_twice}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp defined_twice)
(define-match-fn defined_twice (Store Fn)
  (visit Fn Store))
(define-match-fn defined_twice (Store Fn)
  (visit Fn Store))
}

// expected-error@+6 {{name cannot be exported to both C++ and C: exported_to_both}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp exported_to_both)
(export-c exported_to_both)
(define-match-fn exported_to_both (Store Fn)
  (visit Fn Store))
}

// expected-error@+6 {{internal name cannot also be exported: internal_exported}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-c internal_exported)
(export-c-internal internal_exported)
(define-match-fn internal_exported (Store Fn)
  (visit Fn Store))
}

// expected-error@+5 {{match-fn cannot be exported to C: match_fn_exported_to_c}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-c match_fn_exported_to_c)
(define-match-fn match_fn_exported_to_c (Store Fn)
  (visit Fn Store))
}
