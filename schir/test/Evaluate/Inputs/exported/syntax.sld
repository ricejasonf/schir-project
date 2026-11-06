(import (schir base))

(define-library (exported syntax)
  (import (schir base))
  (begin
    (define (hidden-proc x) (list 'hidden-proc x))
    (define hidden-var 'hidden-var)
    (define-syntax use-hidden
      (syntax-rules ()
        ((use-hidden x) (list (hidden-proc x) hidden-var))))

    (define-syntax use-import
      (syntax-rules ()
        ((use-import x) (list 'imported x))))

    (define-syntax hidden-syntax
      (syntax-rules ()
        ((hidden-syntax x) (list 'hidden-syntax x))))
    (define-syntax use-hidden-syntax
      (syntax-rules ()
        ((use-hidden-syntax x) (hidden-syntax x))))

    (define-syntax define-getter
      (syntax-rules ()
        ((define-getter name value)
         (begin
           (define hidden value)
           (define (name) hidden)))))

    (define-syntax ⌋
      (syntax-rules ()
        ((⌋ a b) (list a b))))
    (define-syntax match-literal
      (syntax-rules (⌋)
        ((match-literal ⌋) 'matched)
        ((match-literal x) 'no-match)))

    (define-syntax my-or
      (syntax-rules ()
        ((my-or) #f)
        ((my-or e) e)
        ((my-or e r ...) ((lambda (t) (if t t (my-or r ...))) e))))

    (define-syntax catch-raise
      (syntax-rules ()
        ((catch-raise x) (guard (e (#t (list 'caught e))) (raise x)))))
    ) ; end begin

  (export use-hidden use-import use-hidden-syntax define-getter
          ⌋ match-literal my-or catch-raise))
