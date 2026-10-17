(import (schir base))

(define-library (my literals)
  (import (schir base))
  (begin
    (define-syntax ⌋
      (syntax-rules ()
        ((⌋ a b) (list a b))))

    (define my-var 5)

    (define-syntax match-literals
      (syntax-rules (⌋ my-var)
        ((match-literals a ⌋ b) 'syntax-matched)
        ((match-literals a my-var b) 'variable-matched)
        ((match-literals x ...) 'no-match)))
    ) ; end begin

  (export ⌋ my-var match-literals))
