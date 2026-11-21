(import (schir base))

;; Define the forms of nbdl specs that are meant for library
;; implementations.
(define-library (nbdl spec impl)
  (export child-stable-mut!)
  (import (schir base))
  (begin
    ;; Mark an operand of visit as one the callee may mutate while leaving
    ;; every store read from it valid.
    (define-syntax child-stable-mut!
      (syntax-rules ()
        ((child-stable-mut! Store)
         (error-with-loc (syntax-source-loc Store)
                         "child-stable-mut! is only valid as an operand of visit"))))))
