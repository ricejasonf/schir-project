(import (schir base))

;; Export theorems, definitions, axioms and the proof terms of the kernel.
;;
;; A theorem, definition or axiom is checked where it is defined and its
;; name is bound to a procedure that applies it to its arguments.
;; Apply a variable with app, as in (app h x).
;;
;; Import (schir base) with (except (schir base) < > + - *) beside
;; (schlap infix).
(define-library (schlap proof)
  (export
    Prop ℕ zero succ ℕ-rec
    λ app
    ⊤-intro ⊥-elim
    ∧-intro ∧-left ∧-right
    ∨-inl ∨-inr ∨-elim
    ∃-intro ∃-elim
    refl subst
    define-theorem define-definition define-axiom
    theorem-axioms)
  (import (except (schir base) < > + - *)
          (schir mlir)
          (schlap core)
          (schlap infix))
  (begin
    (define %check-declaration
      (load-builtin "schlap_check_declaration"))

    (define %discard-declaration
      (load-builtin "schlap_discard_declaration"))

    ;; Lift V to a term or raise an error at Loc.
    (define (%term Loc V)
      (define T (lift* Loc V))
      (unless (value? T)
        (error-with-loc Loc "expecting a term: {}" T))
      T)

    ;; Locate applications of named terms at the form being built.
    (define %current-loc #f)

    (define (%with-loc Loc Thunk)
      (define Saved %current-loc)
      (dynamic-wind
        (lambda () (set! %current-loc Loc))
        Thunk
        (lambda () (set! %current-loc Saved))))

    (define (%create OpName Loc . Operands)
      (result
        (create-op* OpName Loc '()
                    (map (lambda (V) (%term Loc V)) Operands)
                    (list !schlap.unknown)
                    '())))

    ;; Create a binder whose body is the result of BodyFn applied to
    ;; the bound variable at VarLoc.
    (define (%create-lambda Loc VarLoc BodyFn)
      (define Op
        (create-op "schlap.lambda"
                   (loc: Loc)
                   (operands:)
                   (attributes:)
                   (result-types: !schlap.unknown)
                   (region: "body" () 0)))
      (with-builder (lambda ()
        (define Block (entry-block Op))
        (define Var (add-argument Block !schlap.unknown VarLoc))
        (at-block-begin Block)
        (%with-loc Loc (lambda () (yield* Loc (%term Loc (BodyFn Var)))))))
      (result Op))

    ;; Apply F to each argument in turn.
    (define (app* Loc F . Args)
      (let loop ((F (%term Loc F))
                 (Args Args))
        (if (null? Args)
          F
          (loop (%create "schlap.app" Loc F (car Args)) (cdr Args)))))

    ;; Create the constants where an operand is expected.
    (define (Prop Loc) (%create "schlap.prop_sort" Loc))
    (define (ℕ Loc) (%create "schlap.nat" Loc))
    (define (zero Loc) (%create "schlap.zero" Loc))
    (define (refl Loc) (%create "schlap.refl" Loc))
    (define (⊤-intro Loc) (%create "schlap.top_intro" Loc))

    ;; Bind each variable to a nested λ.
    (define-syntax λ
      (syntax-rules ()
        ((_ (X ...) Body1 BodyN ...)
         (%lambdas (syntax-source-loc) (X ...) Body1 BodyN ...))))

    (define-syntax %lambdas
      (syntax-rules ()
        ((_ L () Body ...)
         (let () Body ...))
        ((_ L (X Y ...) Body ...)
         (%create-lambda L (syntax-source-loc X)
                         (lambda (X) (%lambdas L (Y ...) Body ...))))))

    (define-syntax app
      (syntax-rules ()
        ((_ F A ...)
         (app* (syntax-source-loc) F A ...))))

    (define-syntax succ
      (syntax-rules ()
        ((_ N)
         (%create "schlap.succ" (syntax-source-loc) N))))

    (define-syntax ℕ-rec
      (syntax-rules ()
        ((_ Motive Base Step N)
         (%create "schlap.nat_rec" (syntax-source-loc) Motive Base Step N))
        ((_ X ...)
         (error-with-loc (syntax-source-loc)
           "ℕ-rec takes a motive, a base case, a step and a number"))))

    (define-syntax ⊥-elim
      (syntax-rules ()
        ((_ H)
         (%create "schlap.false_elim" (syntax-source-loc) H))))

    (define-syntax ∧-intro
      (syntax-rules ()
        ((_ A B)
         (%create "schlap.and_intro" (syntax-source-loc) A B))
        ((_ X ...)
         (error-with-loc (syntax-source-loc)
           "∧-intro takes a proof of each side"))))

    (define-syntax ∧-left
      (syntax-rules ()
        ((_ H)
         (%create "schlap.and_left" (syntax-source-loc) H))))

    (define-syntax ∧-right
      (syntax-rules ()
        ((_ H)
         (%create "schlap.and_right" (syntax-source-loc) H))))

    (define-syntax ∨-inl
      (syntax-rules ()
        ((_ H)
         (%create "schlap.or_inl" (syntax-source-loc) H))))

    (define-syntax ∨-inr
      (syntax-rules ()
        ((_ H)
         (%create "schlap.or_inr" (syntax-source-loc) H))))

    (define-syntax ∨-elim
      (syntax-rules ()
        ((_ H F G)
         (%create "schlap.or_elim" (syntax-source-loc) H F G))
        ((_ H F)
         (error-with-loc (syntax-source-loc)
           "∨-elim is missing the case for the right side"))
        ((_ X ...)
         (error-with-loc (syntax-source-loc)
           "∨-elim takes a disjunction and a case for each side"))))

    (define-syntax ∃-intro
      (syntax-rules ()
        ((_ W P)
         (%create "schlap.exists_intro" (syntax-source-loc) W P))))

    (define-syntax ∃-elim
      (syntax-rules ()
        ((_ H F)
         (%create "schlap.exists_elim" (syntax-source-loc) H F))))

    (define-syntax subst
      (syntax-rules ()
        ((_ H Motive P)
         (%create "schlap.subst" (syntax-source-loc) H Motive P))))

    ;; Map the name of each checked declaration to its axioms.
    (define %axioms '())

    ;; Return the names of the axioms a declaration depends on.
    (define (theorem-axioms Name)
      (let ((Entry (assq Name %axioms)))
        (unless Entry
          (error "no checked theorem or definition: {}" Name))
        (cdr Entry)))

    ;; Return true if X is the location lift* passes rather than a term.
    (define (%loc? X)
      (not (or (value? X) (procedure? X) (boolean? X)
               (exact-integer? X) (symbol? X))))

    ;; Make the procedure that refers to the declaration Name.
    (define (%named-term Name)
      (lambda Args
        (cond
          ((and (pair? Args) (null? (cdr Args)) (%loc? (car Args)))
           (result
             (create-op "schlap.ref"
                        (loc: (car Args))
                        (operands:)
                        (attributes: ("name" (flat-symbolref-attr Name)))
                        (result-types: !schlap.unknown))))
          ((not %current-loc)
           (error "{} is usable only in a theorem, definition or axiom" Name))
          (else
           (apply app* %current-loc (%named-term Name) Args)))))

    ;; Create a declaration whose regions yield the results of RegionFns,
    ;; check it, and return the procedure that refers to it.
    (define (%declare OpName Loc Name . RegionFns)
      (define Op
        (create-op* OpName Loc
                    (list (list "sym_name" (string-attr Name)))
                    '() '()
                    (map (lambda (Fn) (list '() (lambda () 0))) RegionFns)))
      (guard (E (#t (%discard-declaration Op)
                    (raise E)))
        (%with-loc Loc (lambda ()
          (for-each
            (lambda (Index Fn)
              (with-builder (lambda ()
                (at-block-begin (entry-block (get-region Op Index)))
                (yield* Loc (%term Loc (Fn))))))
            (range (length RegionFns))
            RegionFns))))
      (set! %axioms (cons (cons Name (%check-declaration Op)) %axioms))
      (%named-term Name))

    (define-syntax define-theorem
      (syntax-rules ()
        ((_ Name Statement Proof)
         (define Name
           (%declare "schlap.theorem" (syntax-source-loc Name) 'Name
                     (lambda () Statement)
                     (lambda () Proof))))))

    (define-syntax define-axiom
      (syntax-rules ()
        ((_ Name Statement)
         (define Name
           (%declare "schlap.axiom" (syntax-source-loc Name) 'Name
                     (lambda () Statement))))))

    (define-syntax define-definition
      (syntax-rules (:)
        ((_ (Name (X : T) ...) : R Body)
         (define Name
           (%declare "schlap.definition" (syntax-source-loc Name) 'Name
                     (lambda () (∀ ((X : T) ...) R))
                     (lambda () (λ (X ...) Body)))))
        ((_ Name : R Body)
         (define Name
           (%declare "schlap.definition" (syntax-source-loc Name) 'Name
                     (lambda () R)
                     (lambda () Body))))))
    )) ;; define-library
