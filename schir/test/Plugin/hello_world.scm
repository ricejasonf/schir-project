; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(load-plugin "libSchirHelloWorld.so")
(define my-write
  (load-builtin "schir_hello_world_my_write"))

(define-binding ultimate-answer
                schir_hello_world_ultimate_answer)
(define get-ultimate-answer
  (load-builtin "schir_hello_world_get_ultimate_answer"))

; CHECK: (hello world)
(my-write '(hello world))
(newline)

; CHECK: 42
(set! ultimate-answer 42)
(write ultimate-answer)
(newline)


; CHECK: 42
(write (get-ultimate-answer))
(newline)

