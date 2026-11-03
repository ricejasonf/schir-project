; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: (chat_app.set_draft #t #t)
(write (list (string->symbol "chat_app.set_draft")
             (symbol? (string->symbol "foo"))
             (eq? (string->symbol "foo") 'foo)))
(newline)

; CHECK-NEXT: ("foo" #t "chat_app.set_draft")
(write (list (symbol->string 'foo)
             (string? (symbol->string 'foo))
             (symbol->string
               (string->symbol
                 (string-append "chat_app" "." "set_draft")))))
(newline)
