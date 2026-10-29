// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -ferror-limit=0 -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-note@* 3 {{error raised here}}

// expected-error@+6 {{expecting a member name as a struct key: foo::key}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-struct struct_with_bad_key ()
  (member: '.x 'int (init-args: 1))
  (member: 'foo::key 'int (init-args: 2)))
}

// expected-error@+5 {{expecting a store as the root of a path (see lift-store)}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-fn get_raw_struct ((Sink : 'int)) -> ()
  (visit Sink (get (construct 'int 5) '.x))
  (return))
}

// Keep this case last since an error within match leaves later definitions unchecked.
// expected-error@+5 {{expecting a store to match (see lift-store)}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-fn match_raw_struct () -> ()
  (match (construct 'int 5)
    (else => (lambda (X) (return))))
  (return))
}
