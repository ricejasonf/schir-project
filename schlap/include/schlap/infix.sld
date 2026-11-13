(import (schir base))

;; Export the Schlap notation for SRFI-105 curly-infix (#!curly-infix).
;;
;; Split mixed expressions loosest first: ⇔; ⇒; ∧ ∨; = ≠ < ≤ > ≥ ∈ ∉ ⊆;
;; ∪ ∩; ‥; + -; *.
;; Fold ∧ ∨ ∪ ∩ + - * from the left and ⇒ from the right.
;; Reject a chain of ⇔, of comparisons or of ‥ without braces.
;; Reject ∧ mixed with ∨, or ∪ mixed with ∩, without braces.
;;
;; Import (schir base) with (except (schir base) < > + - *).
(define-library (schlap infix)
  (export
    ∧ ∨ ¬ ⇒ ⇔
    = ≠ < ≤ > ≥
    + - *
    ∈ ∉ ∪ ∩ ⊆ ‥
    ∅ ⊤ ⊥
    ∀ ∃
    $nfx$)
  (import (except (schir base) < > + - *)
          (schlap core))
  (begin
    ;; Create the operation for ∅, ⊤ or ⊥ at its own location.
    (define-syntax %atom
      (syntax-rules ()
        ((_ X)
         (%atom-lit X X))))

    (define-syntax %atom-lit
      (syntax-rules (∅ ⊤ ⊥)
        ((_ ∅ X)
         (empty-set* (syntax-source-loc X)))
        ((_ ⊤ X)
         (true* (syntax-source-loc X)))
        ((_ ⊥ X)
         (false* (syntax-source-loc X)))
        ((_ Y X)
         Y)))

    ;; Fold operands from the left with F at location L.
    (define-syntax %fold-left
      (syntax-rules ()
        ((_ F L A)
         A)
        ((_ F L A B C ...)
         (%fold-left F L (F L A B) C ...))))

    ;; Fold operands from the right with F at location L.
    (define-syntax %fold-right
      (syntax-rules ()
        ((_ F L A)
         A)
        ((_ F L A B C ...)
         (F L A (%fold-right F L B C ...)))))

    (define-syntax ∧
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "∧ is missing an operand"))
        ((_ A B ...)
         (%fold-left and* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax ∨
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "∨ is missing an operand"))
        ((_ A B ...)
         (%fold-left or* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax ¬
      (syntax-rules ()
        ((_ A)
         (not* (syntax-source-loc) (%atom A)))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "¬ takes one operand"))))

    (define-syntax ⇒
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "⇒ is missing an operand"))
        ((_ A B ...)
         (%fold-right implies* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax ⇔
      (syntax-rules ()
        ((_ A B)
         (iff* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc) "⇔ does not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "⇔ takes two operands"))))

    (define-syntax =
      (syntax-rules ()
        ((_ A B)
         (eq* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "= takes two operands"))))

    (define-syntax ≠
      (syntax-rules ()
        ((_ A B)
         (ne* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "≠ takes two operands"))))

    (define-syntax <
      (syntax-rules ()
        ((_ A B)
         (lt* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "< takes two operands"))))

    (define-syntax ≤
      (syntax-rules ()
        ((_ A B)
         (le* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "≤ takes two operands"))))

    (define-syntax >
      (syntax-rules ()
        ((_ A B)
         (gt* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "> takes two operands"))))

    (define-syntax ≥
      (syntax-rules ()
        ((_ A B)
         (ge* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "≥ takes two operands"))))

    (define-syntax ∈
      (syntax-rules ()
        ((_ A B)
         (member* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "∈ takes two operands"))))

    (define-syntax ∉
      (syntax-rules ()
        ((_ A B)
         (not-member* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "∉ takes two operands"))))

    (define-syntax ⊆
      (syntax-rules ()
        ((_ A B)
         (subset* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc)
           "comparisons do not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "⊆ takes two operands"))))

    (define-syntax ∪
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "∪ is missing an operand"))
        ((_ A B ...)
         (%fold-left union* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax ∩
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "∩ is missing an operand"))
        ((_ A B ...)
         (%fold-left intersect* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax ‥
      (syntax-rules ()
        ((_ A B)
         (range* (syntax-source-loc) (%atom A) (%atom B)))
        ((_ A B C D ...)
         (error-with-loc (syntax-source-loc) "‥ does not chain without braces"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc) "‥ takes two operands"))))

    (define-syntax +
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "+ is missing an operand"))
        ((_ A B ...)
         (%fold-left add* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax -
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "- is missing an operand"))
        ((_ A)
         (neg* (syntax-source-loc) (%atom A)))
        ((_ A B ...)
         (%fold-left sub* (syntax-source-loc) (%atom A) (%atom B) ...))))

    (define-syntax *
      (syntax-rules ()
        ((_)
         (error-with-loc (syntax-source-loc) "* is missing an operand"))
        ((_ A B ...)
         (%fold-left mul* (syntax-source-loc) (%atom A) (%atom B) ...))))

    ;; Bind each variable in turn and evaluate the body as a formula.
    (define-syntax ∀
      (syntax-rules ()
        ((_ (B ...) Body1 BodyN ...)
         (%binders forall* (syntax-source-loc) (B ...) Body1 BodyN ...))))

    (define-syntax ∃
      (syntax-rules ()
        ((_ (B ...) Body1 BodyN ...)
         (%binders exists* (syntax-source-loc) (B ...) Body1 BodyN ...))))

    ;; Nest one binder created by F at location L per (X ∈ S) or (X : A).
    (define-syntax %binders
      (syntax-rules (∈ :)
        ((_ F L () Body ...)
         (let () Body ...))
        ((_ F L ((X ∈ S) B ...) Body ...)
         (F L (syntax-source-loc X) (%atom S)
            (lambda (X) (%binders F L (B ...) Body ...))))
        ((_ F L ((X : A) B ...) Body ...)
         (F L (syntax-source-loc X) (%atom A)
            (lambda (X) (%binders F L (B ...) Body ...))))))


    ;; Mixed curly-infix expressions.
    (define-syntax $nfx$
      (syntax-rules ()
        ((_ X ...)
         (%nfx-iff (syntax-source-loc) () X ...))))

    ;; Each splitter below takes EL, the location to report a missing
    ;; operand at, before the operands it has collected.

    ;; Split on ⇔ which does not chain.
    (define-syntax %nfx-iff
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-imp EL () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-iff-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-iff-op
      (syntax-rules (⇔)
        ((_ ⇔ L EL (C ...) X ...)
         (%nfx-iff-rhs L (C ...) () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-iff EL (C ... Y) X ...))))

    (define-syntax %nfx-iff-rhs
      (syntax-rules ()
        ((_ L (C ...) (D ...))
         (iff* L (%nfx-imp L () C ...) (%nfx-imp L () D ...)))
        ((_ L (C ...) (D ...) Y X ...)
         (%nfx-iff-rhs-op Y (syntax-source-loc Y) L (C ...) (D ...) X ...))))

    (define-syntax %nfx-iff-rhs-op
      (syntax-rules (⇔)
        ((_ ⇔ YL L (C ...) (D ...) X ...)
         (error-with-loc YL "⇔ does not chain without braces"))
        ((_ Y YL L (C ...) (D ...) X ...)
         (%nfx-iff-rhs L (C ...) (D ... Y) X ...))))

    ;; Split on ⇒ nesting to the right.
    (define-syntax %nfx-imp
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-and EL () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-imp-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-imp-op
      (syntax-rules (⇒)
        ((_ ⇒ L EL (C ...) X ...)
         (implies* L (%nfx-and L () C ...) (%nfx-imp L () X ...)))
        ((_ Y L EL (C ...) X ...)
         (%nfx-imp EL (C ... Y) X ...))))

    ;; Split on ∧ or ∨ folding from the left.
    (define-syntax %nfx-and
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-cmp EL () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-and-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-and-op
      (syntax-rules (∧ ∨)
        ((_ ∧ L EL (C ...) X ...)
         (%nfx-and-acc (%nfx-cmp L () C ...) ∧ and* L () X ...))
        ((_ ∨ L EL (C ...) X ...)
         (%nfx-and-acc (%nfx-cmp L () C ...) ∨ or* L () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-and EL (C ... Y) X ...))))

    ;; Continue the fold where F applies the pending operator Op at
    ;; location FL.
    (define-syntax %nfx-and-acc
      (syntax-rules ()
        ((_ A Op F FL (C ...))
         (F FL A (%nfx-cmp FL () C ...)))
        ((_ A Op F FL (C ...) Y X ...)
         (%nfx-and-acc-op Y (syntax-source-loc Y) A Op F FL (C ...) X ...))))

    (define-syntax %nfx-and-acc-op
      (syntax-rules (∧ ∨)
        ((_ ∧ L A ∧ F FL (C ...) X ...)
         (%nfx-and-acc (F FL A (%nfx-cmp FL () C ...)) ∧ F L () X ...))
        ((_ ∨ L A ∨ F FL (C ...) X ...)
         (%nfx-and-acc (F FL A (%nfx-cmp FL () C ...)) ∨ F L () X ...))
        ((_ ∧ L A ∨ F FL (C ...) X ...)
         (error-with-loc L "∧ and ∨ cannot be mixed without braces"))
        ((_ ∨ L A ∧ F FL (C ...) X ...)
         (error-with-loc L "∧ and ∨ cannot be mixed without braces"))
        ((_ Y L A Op F FL (C ...) X ...)
         (%nfx-and-acc A Op F FL (C ... Y) X ...))))

    ;; Split on a comparison which does not chain.
    (define-syntax %nfx-cmp
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-set EL () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-cmp-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-cmp-op
      (syntax-rules (= ≠ < ≤ > ≥ ∈ ∉ ⊆)
        ((_ = L EL (C ...) X ...) (%nfx-cmp-rhs eq* L (C ...) () X ...))
        ((_ ≠ L EL (C ...) X ...) (%nfx-cmp-rhs ne* L (C ...) () X ...))
        ((_ < L EL (C ...) X ...) (%nfx-cmp-rhs lt* L (C ...) () X ...))
        ((_ ≤ L EL (C ...) X ...) (%nfx-cmp-rhs le* L (C ...) () X ...))
        ((_ > L EL (C ...) X ...) (%nfx-cmp-rhs gt* L (C ...) () X ...))
        ((_ ≥ L EL (C ...) X ...) (%nfx-cmp-rhs ge* L (C ...) () X ...))
        ((_ ∈ L EL (C ...) X ...) (%nfx-cmp-rhs member* L (C ...) () X ...))
        ((_ ∉ L EL (C ...) X ...)
         (%nfx-cmp-rhs not-member* L (C ...) () X ...))
        ((_ ⊆ L EL (C ...) X ...) (%nfx-cmp-rhs subset* L (C ...) () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-cmp EL (C ... Y) X ...))))

    (define-syntax %nfx-cmp-rhs
      (syntax-rules ()
        ((_ F L (C ...) (D ...))
         (F L (%nfx-set L () C ...) (%nfx-set L () D ...)))
        ((_ F L (C ...) (D ...) Y X ...)
         (%nfx-cmp-rhs-op Y (syntax-source-loc Y) F L (C ...) (D ...) X ...))))

    (define-syntax %nfx-cmp-rhs-op
      (syntax-rules (= ≠ < ≤ > ≥ ∈ ∉ ⊆)
        ((_ = YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ≠ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ < YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ≤ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ > YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ≥ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ∈ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ∉ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ ⊆ YL F L (C ...) (D ...) X ...) (%nfx-cmp-chain YL))
        ((_ Y YL F L (C ...) (D ...) X ...)
         (%nfx-cmp-rhs F L (C ...) (D ... Y) X ...))))

    (define-syntax %nfx-cmp-chain
      (syntax-rules ()
        ((_ YL)
         (error-with-loc YL "comparisons do not chain without braces"))))

    ;; Split on ∪ or ∩ folding from the left.
    (define-syntax %nfx-set
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-range EL () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-set-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-set-op
      (syntax-rules (∪ ∩)
        ((_ ∪ L EL (C ...) X ...)
         (%nfx-set-acc (%nfx-range L () C ...) ∪ union* L () X ...))
        ((_ ∩ L EL (C ...) X ...)
         (%nfx-set-acc (%nfx-range L () C ...) ∩ intersect* L () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-set EL (C ... Y) X ...))))

    ;; Continue the fold where F applies the pending operator Op at
    ;; location FL.
    (define-syntax %nfx-set-acc
      (syntax-rules ()
        ((_ A Op F FL (C ...))
         (F FL A (%nfx-range FL () C ...)))
        ((_ A Op F FL (C ...) Y X ...)
         (%nfx-set-acc-op Y (syntax-source-loc Y) A Op F FL (C ...) X ...))))

    (define-syntax %nfx-set-acc-op
      (syntax-rules (∪ ∩)
        ((_ ∪ L A ∪ F FL (C ...) X ...)
         (%nfx-set-acc (F FL A (%nfx-range FL () C ...)) ∪ F L () X ...))
        ((_ ∩ L A ∩ F FL (C ...) X ...)
         (%nfx-set-acc (F FL A (%nfx-range FL () C ...)) ∩ F L () X ...))
        ((_ ∪ L A ∩ F FL (C ...) X ...)
         (error-with-loc L "∪ and ∩ cannot be mixed without braces"))
        ((_ ∩ L A ∪ F FL (C ...) X ...)
         (error-with-loc L "∪ and ∩ cannot be mixed without braces"))
        ((_ Y L A Op F FL (C ...) X ...)
         (%nfx-set-acc A Op F FL (C ... Y) X ...))))

    ;; Split on ‥ which does not chain.
    (define-syntax %nfx-range
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-add EL () () C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-range-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-range-op
      (syntax-rules (‥)
        ((_ ‥ L EL (C ...) X ...)
         (%nfx-range-rhs L (C ...) () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-range EL (C ... Y) X ...))))

    (define-syntax %nfx-range-rhs
      (syntax-rules ()
        ((_ L (C ...) (D ...))
         (range* L (%nfx-add L () () C ...) (%nfx-add L () () D ...)))
        ((_ L (C ...) (D ...) Y X ...)
         (%nfx-range-rhs-op Y (syntax-source-loc Y) L (C ...) (D ...) X ...))))

    (define-syntax %nfx-range-rhs-op
      (syntax-rules (‥)
        ((_ ‥ YL L (C ...) (D ...) X ...)
         (error-with-loc YL "‥ does not chain without braces"))
        ((_ Y YL L (C ...) (D ...) X ...)
         (%nfx-range-rhs L (C ...) (D ... Y) X ...))))

    ;; Split terms on + and - folding from the left where (N ...) are the
    ;; locations of the unary - before the current term.
    (define-syntax %nfx-add
      (syntax-rules ()
        ((_ EL (N ...) (C ...))
         (%nfx-negs (N ...) (%nfx-mul EL () C ...)))
        ((_ EL (N ...) (C ...) Y X ...)
         (%nfx-add-op Y (syntax-source-loc Y) EL (N ...) (C ...) X ...))))

    (define-syntax %nfx-add-op
      (syntax-rules (+ -)
        ((_ - L EL (N ...) () X ...)
         (%nfx-add L (N ... L) () X ...))
        ((_ + L EL (N ...) (C ...) X ...)
         (%nfx-add-acc (%nfx-negs (N ...) (%nfx-mul L () C ...))
                       add* L () () X ...))
        ((_ - L EL (N ...) (C ...) X ...)
         (%nfx-add-acc (%nfx-negs (N ...) (%nfx-mul L () C ...))
                       sub* L () () X ...))
        ((_ Y L EL (N ...) (C ...) X ...)
         (%nfx-add EL (N ...) (C ... Y) X ...))))

    ;; Continue the fold where F applies the pending operator at
    ;; location FL.
    (define-syntax %nfx-add-acc
      (syntax-rules ()
        ((_ A F FL (N ...) (C ...))
         (F FL A (%nfx-negs (N ...) (%nfx-mul FL () C ...))))
        ((_ A F FL (N ...) (C ...) Y X ...)
         (%nfx-add-acc-op Y (syntax-source-loc Y)
                          A F FL (N ...) (C ...) X ...))))

    (define-syntax %nfx-add-acc-op
      (syntax-rules (+ -)
        ((_ - L A F FL (N ...) () X ...)
         (%nfx-add-acc A F FL (N ... L) () X ...))
        ((_ + L A F FL (N ...) (C ...) X ...)
         (%nfx-add-acc (F FL A (%nfx-negs (N ...) (%nfx-mul FL () C ...)))
                       add* L () () X ...))
        ((_ - L A F FL (N ...) (C ...) X ...)
         (%nfx-add-acc (F FL A (%nfx-negs (N ...) (%nfx-mul FL () C ...)))
                       sub* L () () X ...))
        ((_ Y L A F FL (N ...) (C ...) X ...)
         (%nfx-add-acc A F FL (N ...) (C ... Y) X ...))))

    ;; Negate E once per location in (N ...).
    (define-syntax %nfx-negs
      (syntax-rules ()
        ((_ () E)
         E)
        ((_ (L N ...) E)
         (neg* L (%nfx-negs (N ...) E)))))

    ;; Split factors on * folding from the left.
    (define-syntax %nfx-mul
      (syntax-rules ()
        ((_ EL (C ...))
         (%nfx-operand EL C ...))
        ((_ EL (C ...) Y X ...)
         (%nfx-mul-op Y (syntax-source-loc Y) EL (C ...) X ...))))

    (define-syntax %nfx-mul-op
      (syntax-rules (*)
        ((_ * L EL (C ...) X ...)
         (%nfx-mul-acc (%nfx-operand L C ...) L () X ...))
        ((_ Y L EL (C ...) X ...)
         (%nfx-mul EL (C ... Y) X ...))))

    ;; Continue the fold where the pending * is at location FL.
    (define-syntax %nfx-mul-acc
      (syntax-rules ()
        ((_ A FL (C ...))
         (mul* FL A (%nfx-operand FL C ...)))
        ((_ A FL (C ...) Y X ...)
         (%nfx-mul-acc-op Y (syntax-source-loc Y) A FL (C ...) X ...))))

    (define-syntax %nfx-mul-acc-op
      (syntax-rules (*)
        ((_ * L A FL (C ...) X ...)
         (%nfx-mul-acc (mul* FL A (%nfx-operand FL C ...)) L () X ...))
        ((_ Y L A FL (C ...) X ...)
         (%nfx-mul-acc A FL (C ... Y) X ...))))

    ;; Report a missing operator at the second operand or at EL.
    (define-syntax %nfx-operand
      (syntax-rules ()
        ((_ EL X)
         (%atom X))
        ((_ EL)
         (error-with-loc EL "curly-infix expression is missing an operand"))
        ((_ EL X Y Z ...)
         (error-with-loc (source-loc (syntax-source-loc Y) EL)
                         "curly-infix expression is missing an operator"))))
    )) ;; define-library
