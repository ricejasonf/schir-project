// RUN: clang++ -I %schir_module_path -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s

// Use pragmas since schir-scheme stops at the first error.

// expected-error@+4{{boom}}
#pragma schir_scheme
{
(import (schir base))
(define foo (error "boom"))
}

#pragma schir_scheme
{
(define foo 42)
(if (not (eqv? foo 42)) (error "foo was not redefined"))
}
