; RUN: schir-scheme -I %schir_module_path -I %schlap_module_path %s | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schir mlir)
        (schlap core)
        (schlap proof)
        (schlap infix))

;; Write the message of the error Thunk raises and of each note.
(define (write-error Thunk)
  (guard (E (#t (write (error-object-message E))
                (newline)
                (for-each
                  (lambda (I)
                    (when (error-object? I)
                      (write (error-object-message I))
                      (newline)))
                  (error-object-irritants E))))
    (Thunk)
    (write "no error")
    (newline)))

;; Test that a theorem marked checked by Scheme code is not trusted.

(define-syntax here
  (syntax-rules ()
    ((_) (syntax-source-loc))))

(define Loc (here))

(with-module-builder schlap-current-module
  (lambda ()
    (create-op* "schlap.theorem" Loc
                (list (list "sym_name" (string-attr "forged"))
                      (list "checked" (unit-attr))
                      (list "axioms" (attr "[]")))
                '() '()
                (list (list '() (lambda () (yield* Loc (false* Loc))))
                      (list '() (lambda () (yield* Loc (⊤-intro Loc))))))))

(define (forged Loc)
  (result
    (create-op "schlap.ref"
               (loc: Loc)
               (operands:)
               (attributes: ("name" (flat-symbolref-attr 'forged)))
               (result-types: !schlap.unknown))))

; CHECK: "declaration does not check: uses-forged"
; CHECK-NEXT: "'forged' did not check"
(write-error (lambda ()
  (define-theorem uses-forged ⊥ forged)
  #t))
