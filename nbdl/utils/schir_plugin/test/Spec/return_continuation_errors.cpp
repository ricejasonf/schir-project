// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-note@* 2 {{error raised here}}

// Reject a visit on K in another function via a variable set in the body.
// expected-error@* {{nbdl inference passes failed}}
// expected-note@+12 {{return continuation used outside of its function}}
// expected-note@+7 {{the continuation is bound here}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp ret_outer ret_other)
(define LeakedK #f)
(define-fn ret_outer ((N : 'int)) -> ('int)
  (with-return K)
  (set! LeakedK K)
  (return N))
(define-fn ret_other ((M : 'int)) -> ('int)
  (visit LeakedK M))
(finalize-module)
}

// Reject a with-return clause after another statement.
// expected-error@+7 {{unexpected with-return}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp ret_after_stmt)
(define-fn ret_after_stmt ((N : 'int)) -> ('int)
  (visit '|nbdl::noop_t{}| N)
  (with-return K)
  (visit K N))
}

// Reject a with-return clause in a match-fn.
// expected-error@+6 {{unexpected with-return}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp ret_in_match_fn)
(define-fn ret_in_match_fn ((N : 'int)) -> ('int)
  (visit (match-fn (X) (with-return K) (visit K X)) N))
}
