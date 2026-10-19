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
         (sum-aux (syntax-source-loc) V ...))))

    (define-syntax -
      (syntax-rules ()
        ((- V)
         (negate-aux (syntax-source-loc) V))
        ((- V1 VN ...)
         (%nfx-sub (syntax-source-loc) V1 VN ...))))

    (define-syntax *
      (syntax-rules ()
        ((* V)
         V)
        ((* V1 VN ...)
         (%nfx-fold gprod-aux (syntax-source-loc) V1 VN ...))))

    (define-syntax /
      (syntax-rules ()
        ((/ V)
         (inverse-aux (syntax-source-loc) V))
        ((/ V1 VN ...)
         (%nfx-fold %nfx-div (syntax-source-loc) V1 VN ...))))

    (define-syntax ⌋
      (syntax-rules ()
        ((⌋ V)
         V)
        ((⌋ V1 VN ...)
         (%nfx-fold-right iprod-aux (syntax-source-loc) V1 VN ...))))

    (define-syntax ∧
      (syntax-rules ()
        ((∧ V)
         V)
        ((∧ V1 VN ...)
         (%nfx-fold oprod-aux (syntax-source-loc) V1 VN ...))))

    ;; Sum the first operand with the rest negated at location L.
    (define-syntax %nfx-sub
      (syntax-rules ()
        ((_ L V1 VN ...)
         (let ((Loc L))
           (sum-aux Loc V1 (negate-aux Loc VN) ...)))))

    ;; Fold operands from the left with F at location L.
    (define-syntax %nfx-fold
      (syntax-rules ()
        ((_ F L A)
         A)
        ((_ F L A V VN ...)
         (%nfx-fold F L (F L A V) VN ...))))

    ;; Fold operands from the right with F at location L.
    (define-syntax %nfx-fold-right
      (syntax-rules ()
        ((_ F L V)
         V)
        ((_ F L V1 V2 VN ...)
         (F L V1 (%nfx-fold-right F L V2 VN ...)))))

    ;; Mixed curly-infix expressions.
    (define-syntax $nfx$
      (syntax-rules ()
        (($nfx$ X ...)
         (%nfx-sum () #f %nfx-pos #f () X ...))))

    ;; Split terms on + and - where F applies the sign to the current term
    ;; at location FL and SL is the location of the first binary operator.
    (define-syntax %nfx-sum
      (syntax-rules ()
        ((_ () SL F FL (C ...))
         (F FL C ...))
        ((_ (T ...) SL F FL (C ...))
         (sum-aux SL T ... (F FL C ...)))
        ((_ (T ...) SL F FL (C ...) Y X ...)
         (%nfx-sum-op Y (syntax-source-loc Y) (T ...) SL F FL (C ...) X ...))))

    ;; Handle Y at location L if it is + or - otherwise add it to the term.
    (define-syntax %nfx-sum-op
      (syntax-rules (+ -)
        ((_ + L (T ...) SL F FL () X ...)
         (%nfx-sum (T ...) SL F FL () X ...))
        ((_ - L (T ...) SL F FL () X ...)
         (%nfx-sum (T ...) SL %nfx-neg L () X ...))
        ((_ + L () SL F FL (C ...) X ...)
         (%nfx-sum ((F FL C ...)) L %nfx-pos #f () X ...))
        ((_ - L () SL F FL (C ...) X ...)
         (%nfx-sum ((F FL C ...)) L %nfx-neg L () X ...))
        ((_ + L (T ...) SL F FL (C ...) X ...)
         (%nfx-sum (T ... (F FL C ...)) SL %nfx-pos #f () X ...))
        ((_ - L (T ...) SL F FL (C ...) X ...)
         (%nfx-sum (T ... (F FL C ...)) SL %nfx-neg L () X ...))
        ((_ Y L (T ...) SL F FL (C ...) X ...)
         (%nfx-sum (T ...) SL F FL (C ... Y) X ...))))

    (define-syntax %nfx-pos
      (syntax-rules ()
        ((_ L C ...)
         (%nfx-prod () C ...))))

    (define-syntax %nfx-neg
      (syntax-rules ()
        ((_ L C ...)
         (negate-aux L (%nfx-prod () C ...)))))

    ;; Split factors on * and / folding from the left.
    (define-syntax %nfx-prod
      (syntax-rules ()
        ((_ (C ...))
         (%nfx-lc () C ...))
        ((_ (C ...) Y X ...)
         (%nfx-prod-op Y (syntax-source-loc Y) (C ...) X ...))))

    ;; Start the fold if Y at location L is * or / otherwise add it to the factor.
    (define-syntax %nfx-prod-op
      (syntax-rules (* /)
        ((_ * L (C ...) X ...)
         (%nfx-prod-acc (%nfx-lc () C ...) gprod-aux L () X ...))
        ((_ / L (C ...) X ...)
         (%nfx-prod-acc (%nfx-lc () C ...) %nfx-div L () X ...))
        ((_ Y L (C ...) X ...)
         (%nfx-prod (C ... Y) X ...))))

    ;; Continue the fold where F applies the pending operator at location FL.
    (define-syntax %nfx-prod-acc
      (syntax-rules ()
        ((_ A F FL (C ...))
         (F FL A (%nfx-lc () C ...)))
        ((_ A F FL (C ...) Y X ...)
         (%nfx-prod-acc-op Y (syntax-source-loc Y) A F FL (C ...) X ...))))

    ;; Apply the pending operator if Y at location L is * or /
    ;; otherwise add it to the factor.
    (define-syntax %nfx-prod-acc-op
      (syntax-rules (* /)
        ((_ * L A F FL (C ...) X ...)
         (%nfx-prod-acc (F FL A (%nfx-lc () C ...)) gprod-aux L () X ...))
        ((_ / L A F FL (C ...) X ...)
         (%nfx-prod-acc (F FL A (%nfx-lc () C ...)) %nfx-div L () X ...))
        ((_ Y L A F FL (C ...) X ...)
         (%nfx-prod-acc A F FL (C ... Y) X ...))))

    (define-syntax %nfx-div
      (syntax-rules ()
        ((_ L A B)
         (gprod-aux L A (inverse-aux L B)))))

    ;; Split on ⌋ nesting to the right.
    (define-syntax %nfx-lc
      (syntax-rules ()
        ((_ (C ...))
         (%nfx-wedge () C ...))
        ((_ (C ...) Y X ...)
         (%nfx-lc-op Y (syntax-source-loc Y) (C ...) X ...))))

    (define-syntax %nfx-lc-op
      (syntax-rules (⌋)
        ((_ ⌋ L (C ...) X ...)
         (iprod-aux L (%nfx-wedge () C ...) (%nfx-lc () X ...)))
        ((_ Y L (C ...) X ...)
         (%nfx-lc (C ... Y) X ...))))

    ;; Split on ∧ nesting to the right since it is associative.
    (define-syntax %nfx-wedge
      (syntax-rules ()
        ((_ (C ...))
         (%nfx-operand C ...))
        ((_ (C ...) Y X ...)
         (%nfx-wedge-op Y (syntax-source-loc Y) (C ...) X ...))))

    (define-syntax %nfx-wedge-op
      (syntax-rules (∧)
        ((_ ∧ L (C ...) X ...)
         (oprod-aux L (%nfx-operand C ...) (%nfx-wedge () X ...)))
        ((_ Y L (C ...) X ...)
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
