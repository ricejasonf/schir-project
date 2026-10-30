// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only -ferror-limit=0 -Xclang -verify %s

#include <nbdl/spec.hpp>

// expected-note@* {{error raised here}}

// expected-error@+10 {{parameter type is not mappable to C++}}
#pragma schir_scheme
{
(import (nbdl spec)
        (only (schir mlir) type))
(define i32 (type "i32"))

(export-cpp cpp_param)
(define-struct param_s (X)
  (member: '.x i32 (init-args: X)))
(define-fn cpp_param ((P : param_s)) -> ()
  (return))
(finalize-module)
}

// expected-error@+7 {{result type is not mappable to C++}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-c c_result)
(define-struct result_s (X)
  (member: '.x i32 (init-args: X)))
(define-fn c_result ((X : i32)) -> (result_s)
  (return (construct result_s X)))
(finalize-module)
}

// expected-error@+8 {{type is not mappable to C++}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp cpp_body)
(define-struct body_s (X)
  (member: '.x i32 (init-args: X)))
(define-fn cpp_body ((X : 'int)) -> ('int)
  (return (construct body_s X)))
(finalize-module)
}

// expected-error@* {{nbdl inference passes failed}}
// expected-note@+7 {{struct member_s has no member: y}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-struct member_s (X)
  (member: '.x i32 (init-args: X)))
(define-fn no_member ((P : member_s)) -> ()
  (match (get (lift-store P) '.y)
    (else => (lambda (Y) (return)))))
(finalize-module)
}

// Use a struct from a finalized module.
// expected-error@+4 {{struct is not defined: member_s}}
#pragma schir_scheme
{
(import (nbdl spec))
(define-fn not_defined ((P : member_s)) -> ()
  (return))
(finalize-module)
}
