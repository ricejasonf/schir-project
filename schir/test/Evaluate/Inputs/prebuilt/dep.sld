(import (schir base))

(define-library (prebuilt dep)
  (import (schir base))
  (begin
    (define-syntax dep-list
      (syntax-rules ()
        ((dep-list x) (list 'dep x)))))
  (export dep-list))

(write "dep from source")
(newline)
