// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics

#pragma schir_scheme
{
(import (schir base)
        (schir clang))
;(flush-tokens)
(define TheAnswer (expr-eval "41 + 1"))
(write-lexer 0 ; // Loc
             " struct some_foo { static constexpr int value = "
             (number->string TheAnswer)
             "; };")
(flush-tokens)
(unless (expr-eval "some_foo::value == 42")
  (error "some_foo::value is not 42!?"))
(write-lexer "struct some_bar { };")
(flush-tokens)
(unless (expr-eval "__is_empty(some_bar)")
  (error "some_bar is not empty!?"))
(flush-tokens)
(flush-tokens)
((lambda ()
  (write-lexer "struct some_baz { };")
  (flush-tokens)
  (unless (expr-eval "__is_same(some_baz, some_baz)")
    (error "some_baz is not reflexive!?"))))
}

// Make sure we actually did stuff.
static_assert(some_foo::value == 42);
static_assert(__is_same(some_bar, some_bar));
static_assert(__is_same(some_baz, some_baz));
