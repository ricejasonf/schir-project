(import (schir base))

(define-library (my nested-begin)
  (import (schir base))
  (begin
    (define (id V) V)
    (define (get-w) w)
    (begin
      (define x (id 1))
      (define y (id 2)))
    (begin
      (define z 3))
    ;; Define a forward referenced global after another define.
    (begin
      (define v 4)
      (define w (id v))))
  (export x y z get-w))
