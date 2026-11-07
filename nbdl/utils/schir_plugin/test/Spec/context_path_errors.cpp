// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -ferror-limit=0 -Xclang -verify %s

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>

// expected-note@* 0+ {{error raised here}}

// expected-error@+5 {{expecting a member name, a tag, or a nested path from the same root as a path key}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-match-fn runtime_key (Ctx Key Fn)
  (path-type (get Ctx '.names Key))
  (visit Fn Ctx))
}

// expected-error@+5 {{expecting a member name, a tag, or a nested path from the same root as a path key}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-match-fn other_root (Ctx Msg Fn)
  (path-type (get Ctx '.names (get Msg '.id)))
  (visit Fn Ctx))
}

// expected-error@+5 {{expecting a member name, a tag, or a nested path from the same root as a path key: foo::tag}}
#pragma schir_scheme
{
(import (nbdl spec) (nbdl spec context))
(define-match-fn constexpr_key (Ctx Fn)
  (path-type (get Ctx '.a 'foo::tag))
  (visit Fn Ctx))
}
