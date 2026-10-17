(import (schir base))

;; Infix operators for use with SRFI-105 curly-infix (#!curly-infix).
;;
;;   {A ∧ B}  outer product
;;   {A ⌋ B}  left contraction
;;   {A * B}  geometric product
;;   {A / B}  geometric product with the inverse of B
;;   {A + B}  sum
;;   {A - B}  sum with B negated
;;
;; Mixed expressions follow GA4CS precedence where the outer product
;; binds tighter than the left contraction which binds tighter than the
;; geometric product. The sum binds loosest.
;; The left contraction is right associative and the rest are left
;; associative.
;;
;; These conflict with the arithmetic in (schir base) so import
;; that with (except (schir base) + - * /).
(define-library (geomalg infix)
  (export
    + - * / ⌋ ∧
    $nfx$)
  (import (except (schir base) + - * /)
          (geomalg base))
  (begin
    (define-syntax +
      (syntax-rules ()
        ((+ V ...)
         (sum V ...))))

    (define-syntax -
      (syntax-rules ()
        ((- V)
         (negate V))
        ((- V1 VN ...)
         (sum V1 (negate VN) ...))))

    (define-syntax *
      (syntax-rules ()
        ((* V)
         V)
        ((* V1 V2 VN ...)
         (* (gprod V1 V2) VN ...))))

    (define-syntax /
      (syntax-rules ()
        ((/ V)
         (inverse V))
        ((/ V1 V2)
         (gprod V1 (inverse V2)))
        ((/ V1 V2 VN ...)
         (/ (/ V1 V2) VN ...))))

    (define-syntax ⌋
      (syntax-rules ()
        ((⌋ V)
         V)
        ((⌋ V1 VN ...)
         (iprod V1 (⌋ VN ...)))))

    (define-syntax ∧
      (syntax-rules ()
        ((∧ V)
         V)
        ((∧ V1 V2 VN ...)
         (∧ (oprod V1 V2) VN ...))))

    ;; Mixed curly-infix expressions.
    (define-syntax $nfx$
      (syntax-rules ()
        (($nfx$ X ...)
         (%nfx-sum () %nfx-pos () X ...))))

    ;; Split terms on + and - where F applies the sign to the current term.
    (define-syntax %nfx-sum
      (syntax-rules (+ -)
        ((_ () F (C ...))
         (F C ...))
        ((_ (T ...) F (C ...))
         (sum T ... (F C ...)))
        ((_ (T ...) F () + X ...)
         (%nfx-sum (T ...) F () X ...))
        ((_ (T ...) F () - X ...)
         (%nfx-sum (T ...) %nfx-neg () X ...))
        ((_ (T ...) F (C ...) + X ...)
         (%nfx-sum (T ... (F C ...)) %nfx-pos () X ...))
        ((_ (T ...) F (C ...) - X ...)
         (%nfx-sum (T ... (F C ...)) %nfx-neg () X ...))
        ((_ (T ...) F (C ...) Y X ...)
         (%nfx-sum (T ...) F (C ... Y) X ...))))

    (define-syntax %nfx-pos
      (syntax-rules ()
        ((_ C ...)
         (%nfx-prod () C ...))))

    (define-syntax %nfx-neg
      (syntax-rules ()
        ((_ C ...)
         (negate (%nfx-prod () C ...)))))

    ;; Split factors on * and / folding from the left.
    (define-syntax %nfx-prod
      (syntax-rules (* /)
        ((_ (C ...))
         (%nfx-lc () C ...))
        ((_ (C ...) * X ...)
         (%nfx-prod-acc (%nfx-lc () C ...) %nfx-mul () X ...))
        ((_ (C ...) / X ...)
         (%nfx-prod-acc (%nfx-lc () C ...) %nfx-div () X ...))
        ((_ (C ...) Y X ...)
         (%nfx-prod (C ... Y) X ...))))

    ;; Continue the fold where F applies the pending operator.
    (define-syntax %nfx-prod-acc
      (syntax-rules (* /)
        ((_ A F (C ...))
         (F A (%nfx-lc () C ...)))
        ((_ A F (C ...) * X ...)
         (%nfx-prod-acc (F A (%nfx-lc () C ...)) %nfx-mul () X ...))
        ((_ A F (C ...) / X ...)
         (%nfx-prod-acc (F A (%nfx-lc () C ...)) %nfx-div () X ...))
        ((_ A F (C ...) Y X ...)
         (%nfx-prod-acc A F (C ... Y) X ...))))

    (define-syntax %nfx-mul
      (syntax-rules ()
        ((_ A B)
         (gprod A B))))

    (define-syntax %nfx-div
      (syntax-rules ()
        ((_ A B)
         (gprod A (inverse B)))))

    ;; Split on ⌋ nesting to the right.
    (define-syntax %nfx-lc
      (syntax-rules (⌋)
        ((_ (C ...))
         (%nfx-wedge () C ...))
        ((_ (C ...) ⌋ X ...)
         (iprod (%nfx-wedge () C ...) (%nfx-lc () X ...)))
        ((_ (C ...) Y X ...)
         (%nfx-lc (C ... Y) X ...))))

    ;; Split on ∧ nesting to the right since it is associative.
    (define-syntax %nfx-wedge
      (syntax-rules (∧)
        ((_ (C ...))
         (%nfx-operand C ...))
        ((_ (C ...) ∧ X ...)
         (oprod (%nfx-operand C ...) (%nfx-wedge () X ...)))
        ((_ (C ...) Y X ...)
         (%nfx-wedge (C ... Y) X ...))))

    (define-syntax %nfx-operand
      (syntax-rules ()
        ((_ X)
         X)
        ((_)
         (syntax-error "curly-infix expression is missing an operand"))
        ((_ X Y ...)
         (syntax-error "curly-infix expression is missing an operator"
                       X Y ...))))
    )) ;; define-library
