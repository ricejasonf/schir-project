// RUN: clang++ -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s

// Note that a note requires an error or a warning in order to be displayed.
// expected-error@23{{An error occurred}}
// expected-note@12{{The argument was defined here: foo}}
// expected-note@14{{The argument was defined here: (foo bar)}}
namespace foo {
#pragma schir_scheme
{
(import (schir builtins)
        (schir clang))
(define foo 'foo)
(define bar "bar")
(define baz '(foo bar))

(define-syntax maybe-error-note
  (syntax-rules ()
    ((maybe-error-note x)
      (if (source-loc-valid x)
        (error-note "The argument was defined here: {}" x)))))

(define (do-the-thing x y z w)
  (error "An error occurred"
         (maybe-error-note x)
         (maybe-error-note y)
         (maybe-error-note z)
         (maybe-error-note w)))


(do-the-thing foo bar baz 5)
}
} // namespace foo
