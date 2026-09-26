// RUN: clang++ -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s

// A commented datum that leaves an abbreviation (ie quote) with
// nothing before the closing brace is an error, and clang must
// resume parsing immediately after that brace.
namespace foo {
#pragma schir_scheme
{
(import (schir builtins))
'#;(define bar 'bar) } // expected-error {{expected expression}}
static_assert(false, "resumed"); // expected-error {{resumed}}

#pragma schir_scheme
{
(foo #;comment }) // expected-error {{extraneous closing brace}} \
                  // expected-error {{expected unqualified-id}}
} // namespace foo

// Errors not raised by the closing brace itself must still stop at it.
namespace bar {
#pragma schir_scheme
{
(a . b }) // expected-error {{invalid dot notation}} \
          // expected-error {{expected unqualified-id}}
} // namespace bar
