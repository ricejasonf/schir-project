// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Errors are reported at the name of the definition.
// expected-note@* 8 {{error raised here}}

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

// expected-error@+6 {{name cannot be exported to both C++ and a shader: exported_to_cpp_and_shader}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp exported_to_cpp_and_shader)
(export-shader exported_to_cpp_and_shader)
(define-match-fn exported_to_cpp_and_shader (Store Fn)
  (visit Fn Store))
}

// expected-error@+5 {{match-fn cannot be exported to a shader: match_fn_exported_to_shader}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader match_fn_exported_to_shader)
(define-match-fn match_fn_exported_to_shader (Store Fn)
  (visit Fn Store))
}

// expected-error@+4 {{TODO shader entry points are not supported: shader_main}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader shader_helper (shader_main "Fragment" Color))
}

// expected-error@+4 {{expecting name or (name "Kind" GlobalVar ...): "not_a_name"}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader "not_a_name")
}
