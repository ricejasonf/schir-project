// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -ferror-limit=0 -Xclang -verify %s

//
// Copyright Jason Rice 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>

// expected-note@* 3 {{error raised here}}

// expected-error@+4 {{define-member outside of define-context: .x}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-member '.x 'int 1)
}

// expected-error@+6 {{context member defined more than once: .x}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-context context_with_duplicate ()
  (define-member '.x 'int 1)
  (define-member '.x 'int 2))
}

// expected-error@+5 {{expecting a member name as a struct key: foo::key}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-context context_with_bad_key ()
  (define-member 'foo::key 'int 1))
}
