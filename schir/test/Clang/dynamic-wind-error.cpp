// RUN: clang++ -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s

// FIXME Create a way to test these without depending on clang pragmas.
//       (This test is not related to clang.)

// expected-error@+7{{boom}}
#pragma schir_scheme
{
(import (schir builtins))
(define flag #f)
(dynamic-wind
  (lambda () (set! flag #t))
  (lambda () (error "boom"))
  (lambda () (set! flag #f)))
}

#pragma schir_scheme
{
(if flag (error "flag is still set"))
}

// expected-error@+9{{reentry failed}}
#pragma schir_scheme
{
(define reenter-k #f)
(define is-reentry #f)
(define is-after-reentry #f)
(dynamic-wind
  (lambda ()
    (if is-reentry
      (error "reentry failed")))
  (lambda () (call/cc (lambda (k) (set! reenter-k k))))
  (lambda () (if is-reentry (set! is-after-reentry #t))))
(if is-reentry #f (begin (set! is-reentry #t) (reenter-k 0)))
}

#pragma schir_scheme
{
(if is-after-reentry (error "after-thunk ran without reentry"))
}
