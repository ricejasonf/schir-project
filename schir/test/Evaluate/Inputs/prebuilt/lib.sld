(import (schir base))

(define-library (prebuilt lib)
  (import (schir base)
          (prebuilt dep))
  (begin
    (define-syntax lib-list
      (syntax-rules ()
        ((lib-list x) (list 'lib (dep-list x))))))
  (export lib-list))

(write "lib from source")
(newline)
