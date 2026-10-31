// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-note@* 2 {{error raised here}}

// Reject a define-fn within the body of a define-fn.
// expected-error@+6 {{definition must be at the top level}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp def_outer_fn)
(define-fn def_outer_fn ((N : 'int)) -> ('int)
  (define-fn def_inner_fn ((M : 'int)) -> ('int)
    (return M))
  (return N))
}

// Reject a define-match-fn within the body of a define-fn.
// expected-error@+6 {{definition must be at the top level}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp def_outer_match_fn)
(define-fn def_outer_match_fn ((N : 'int)) -> ('int)
  (define-match-fn def_inner_match_fn (X)
    (return X))
  (visit def_inner_match_fn N))
}
