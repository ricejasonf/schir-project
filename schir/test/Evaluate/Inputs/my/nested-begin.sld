(import (schir base))

(define-library (my nested-begin)
  (import (schir base))
  (begin
    (define (id V) V)
    (begin
      (define x (id 1))
      (define y (id 2)))
    (begin
      (define z 3)))
  (export x y z))
