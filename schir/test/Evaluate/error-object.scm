; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: (#t #f #f)
(write (list (error-object? (guard (e (#t e)) (error "boom")))
             (error-object? 'boom)
             (error-object? "boom")))
(newline)

; CHECK-NEXT: ("bad value: 1 two" (1 two))
(write (guard (e ((error-object? e)
                  (list (error-object-message e)
                        (error-object-irritants e))))
         (error "bad value: {} {}" 1 'two)))
(newline)

; CHECK-NEXT: ()
(write (guard (e ((error-object? e) (error-object-irritants e)))
         (error "no irritants")))
(newline)

; CHECK-NEXT: (#t #t)
(write (guard (e ((error-object? e)
                  (list (string? (error-object-message e))
                        (let ((Irrs (error-object-irritants e)))
                          (or (null? Irrs) (pair? Irrs))))))
         (car 5)))
(newline)

; CHECK-NEXT: (sym boom)
(write (guard (e ((error-object? e) 'error)
                 ((symbol? e) (list 'sym e)))
         (raise 'boom)))
(newline)

; CHECK-NEXT: "note"
(write (error-object-message (error-note "note" 'x)))
(newline)
