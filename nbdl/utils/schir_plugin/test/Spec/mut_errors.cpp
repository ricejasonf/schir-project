// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-note@* 2 {{error raised here}}

// Reject mut! within a path.
// expected-error@+6 {{mut! is only valid as an operand of visit}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp mut_in_get)
(define-match-fn mut_in_get (X Fn)
  (visit Fn (get (mut! X) '.a)))
}

// Reject mut! on the callee of a visit.
// expected-error@+6 {{mut! is only valid as an operand of visit}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp mut_callee)
(define-match-fn mut_callee (X Fn)
  (visit (mut! Fn) X))
}
