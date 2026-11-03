; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: (sym boom)
(write (guard (e ((string? e) 'str)
                 ((symbol? e) (list 'sym e)))
         (raise 'boom)))
(newline)

; CHECK-NEXT: (else 42)
(write (guard (e (#f 'no)
                 (else (list 'else e)))
         (raise 42)))
(newline)

; CHECK-NEXT: 7
(write (guard (e ((assq 'a e) => cdr))
         (raise '((a . 7)))))
(newline)

; CHECK-NEXT: normal
(write (guard (e (#t 'caught))
         'normal))
(newline)

; CHECK-NEXT: caught
(write (guard (e (#t 'caught))
         (error "boom")))
(newline)

; CHECK-NEXT: 50
(write (with-exception-handler
         (lambda (e) (+ e 1))
         (lambda () (* 10 (raise-continuable 4)))))
(newline)

; Reraise to an outer guard through dynamic-wind.
; CHECK-NEXT: before
; CHECK-NEXT: after
; CHECK-NEXT: (outer deep)
(write (guard (outer (#t (list 'outer outer)))
         (dynamic-wind
           (lambda () (write 'before) (newline))
           (lambda ()
             (guard (inner ((string? inner) 'wrong))
               (raise 'deep)))
           (lambda () (write 'after) (newline)))))
(newline)
