// RUN: clang++ -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics

#pragma schir_scheme
{
(import (schir builtins))

(define-library (test lib)
  (export my-write
          get-ultimate-answer)
  (import (schir builtins)
          (schir clang))
  (begin
    (load-plugin "libSchirHelloWorld.so")
    (define my-write
      (load-builtin "schir_hello_world_my_write"))
    (define get-ultimate-answer
      (load-builtin "schir_hello_world_get_ultimate_answer"))
    (define-binding ultimate-answer
                    schir_hello_world_ultimate_answer)
    (set! ultimate-answer 42)
  ));

(import (test lib)
        (schir clang))

(define forty-two 'forty-two)

(write-lexer forty-two "static constexpr int forty_two = ")
(write-lexer forty-two (number->string (get-ultimate-answer)))
(write-lexer 0 ";")
}

static_assert(forty_two == 42);
int main() { }
