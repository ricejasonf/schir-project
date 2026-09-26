// RUN: clang++ -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics

// A commented datum immediately preceding the closing brace
// must not cause the brace to be parsed as an expression.
namespace foo {
#pragma schir_scheme
{
(import (schir builtins))
(define foo 'foo)
#;(define bar 'bar)
}

#pragma schir_scheme
{
#;(define baz 'baz) #;(define qux 'qux)
}

struct after_scheme { };
} // namespace foo
