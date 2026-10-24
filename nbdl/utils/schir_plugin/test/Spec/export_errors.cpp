// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// Errors are reported at the name of the definition.
// expected-note@* 15 {{error raised here}}

// Finalize before the other cases leave partial definitions in the module.
// expected-error@* {{mlir pass pipeline failed}}
// expected-note@+5 {{unsupported shader interface type: 'vector<8xf32>'}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_array_input)
(define-shader-entrypoint entry_array_input "Fragment"
    ((X : (type "vector<8xf32>"))) -> ()
  (return))
(finalize-module)
}

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

// expected-error@+6 {{shader entrypoint cannot be exported to C or C++: entry_exported_to_c}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-c entry_exported_to_c)
(export-shader entry_exported_to_c)
(define-shader-entrypoint entry_exported_to_c "Fragment" () -> ()
  (return))
}

// expected-error@+4 {{shader entrypoint is not exported via export-shader: entry_not_exported}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-shader-entrypoint entry_not_exported "Fragment" () -> ()
  (return))
}

// expected-error@+8 {{shader entrypoint cannot be called: entry_called}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_called calls_entry)
(define-shader-entrypoint entry_called "Fragment" () -> ()
  (return))
(define-fn calls_entry () -> ()
  (visit entry_called))
}

// expected-error@+5 {{unknown shader stage: "Geometry"}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_unknown_stage)
(define-shader-entrypoint entry_unknown_stage "Geometry" () -> ()
  (return))
}

// expected-error@+5 {{compute stage requires a local size: ("Compute" X Y Z)}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_compute_size)
(define-shader-entrypoint entry_compute_size "Compute" () -> ()
  (return))
}

// expected-error@+5 {{shader entrypoint types cannot be !nbdl.unknown: entry_unknown_type}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_unknown_type)
(define-shader-entrypoint entry_unknown_type "Fragment" () -> (!nbdl.unknown)
  (return))
}

// expected-error@+6 {{shader entrypoint cannot return a uniform-buffer: entry_uniform_result}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-shader entry_uniform_result)
(define Uniform (shader-global 'uniform-buffer (type "f32")))
(define-shader-entrypoint entry_uniform_result "Fragment" () -> (Uniform)
  (return))
}

// expected-error@+4 {{unknown shader-global kind: push-constant}}
#pragma schir_scheme
{
(import (nbdl spec))
(define PushConstant (shader-global 'push-constant (type "f32")))
}

// expected-error@+4 {{unknown shader builtin: "Color"}}
#pragma schir_scheme
{
(import (nbdl spec))
(define Color (shader-global 'builtin "Color" (type "vector<4xf32>")))
}
