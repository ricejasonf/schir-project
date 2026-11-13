(import (schir base))

;; Define and check specifications of state machines.
;;
;; A specification is a schlap.spec operation in the Schlap module. Its
;; body is an ordinary Scheme body evaluated with the insertion point in
;; the specification, so a procedure defined in it is expanded where it
;; is called.
;;
;; Import (schir base) with (except (schir base) < > + - *) beside
;; (schlap infix).
(define-library (schlap base)
  (export
    define-spec variables init action next invariant
    check check*
    ′ unchanged ite
    set-of tuple tuple-ref
    ↦ @ update)
  (import (except (schir base) < > + - *)
          (schir mlir)
          (schlap core)
          (only (schlap infix) ∈))
  (begin
    (define %schlap-check
      (load-builtin "schlap_check"))

    ;; Create an operation with lifted operands and return its result.
    (define (%create-val OpName Loc . Operands)
      (result
        (create-op* OpName Loc '()
                    (map (lambda (V) (lift* Loc V)) Operands)
                    (list !schlap.unknown)
                    '())))

    ;; Create an operation whose region yields the formula of FormulaFn.
    (define (%create-formula-op OpName Loc Attrs FormulaFn)
      (create-op* OpName Loc Attrs '() '()
                  (list (list '() (lambda () (yield* Loc (FormulaFn)))))))

    (define (spec* Loc Name BodyFn)
      (define Op #f)
      (with-module-builder schlap-current-module
        (lambda ()
          (set! Op
            (create-op* "schlap.spec" Loc
                        (list (list "sym_name" (string-attr Name)))
                        '() '() (list (list '() (lambda () #f)))))))
      (with-builder
        (lambda ()
          (at-block-begin (entry-block Op))
          (BodyFn)))
      Op)

    ;; Define Name as a specification whose body is Body.
    (define-syntax define-spec
      (syntax-rules ()
        ((_ Name Body ...)
         (define Name
           (spec* (syntax-source-loc Name) 'Name
                  (lambda () Body ... (if #f #f)))))))

    (define (variable* Loc Name)
      (result
        (create-op* "schlap.variable" Loc
                    (list (list "name" (string-attr Name)))
                    '() (list !schlap.unknown) '())))

    ;; Define each name as a state variable.
    (define-syntax variables
      (syntax-rules ()
        ((_ Name ...)
         (begin
           (define Name (variable* (syntax-source-loc Name) 'Name))
           ...))))

    (define (init* Loc FormulaFn)
      (%create-formula-op "schlap.init" Loc '() FormulaFn)
      (if #f #f))

    ;; State the initial condition.
    (define-syntax init
      (syntax-rules ()
        ((_ Formula)
         (init* (syntax-source-loc) (lambda () Formula)))))

    ;; Create an action and return the procedure that refers to it.
    (define (action* Loc Name FormulaFn)
      (%create-formula-op "schlap.action" Loc
                          (list (list "sym_name" (string-attr Name)))
                          FormulaFn)
      (lambda (RefLoc)
        (result
          (create-op* "schlap.action_ref" RefLoc
                      (list (list "action" (flat-symbolref-attr Name)))
                      '() (list !schlap.unknown) '()))))

    ;; Define Name as an action that a formula of next can refer to.
    (define-syntax action
      (syntax-rules ()
        ((_ Name Formula)
         (define Name
           (action* (syntax-source-loc Name) 'Name (lambda () Formula))))))

    (define (next* Loc FormulaFn)
      (%create-formula-op "schlap.next" Loc '() FormulaFn)
      (if #f #f))

    ;; State the next-state relation.
    (define-syntax next
      (syntax-rules ()
        ((_ Formula)
         (next* (syntax-source-loc) (lambda () Formula)))))

    (define (invariant* Loc Name FormulaFn)
      (%create-formula-op "schlap.invariant" Loc
                          (list (list "sym_name" (string-attr Name)))
                          FormulaFn)
      (if #f #f))

    ;; State a formula that holds in every reachable state.
    (define-syntax invariant
      (syntax-rules ()
        ((_ Name Formula)
         (invariant* (syntax-source-loc Name) 'Name (lambda () Formula)))))

    ;; Check Spec and return its number of distinct states.
    ;; Options are 'state-limit N and 'deadlock #f.
    (define (check* Loc Spec . Options)
      (define StateLimit 1000000)
      (define Deadlock #t)
      (let loop ((Opts Options))
        (cond
          ((null? Opts) #t)
          ((or (null? (cdr Opts)) (not (symbol? (car Opts))))
           (error-with-loc Loc "expecting option and value: {}" Opts))
          ((eq? (car Opts) 'state-limit)
           (set! StateLimit (cadr Opts))
           (loop (cddr Opts)))
          ((eq? (car Opts) 'deadlock)
           (set! Deadlock (cadr Opts))
           (loop (cddr Opts)))
          (else
           (error-with-loc Loc "unknown check option: {}" (car Opts)))))
      (%schlap-check Spec Loc StateLimit Deadlock))

    ;; Check a specification.
    (define-syntax check
      (syntax-rules ()
        ((_ Spec Option ...)
         (check* (syntax-source-loc) Spec Option ...))))

    ;; Take the value of a state variable in the next state.
    (define-syntax ′
      (syntax-rules ()
        ((_ Var)
         (%create-val "schlap.prime" (syntax-source-loc) Var))))

    ;; State that each variable keeps its value.
    (define-syntax unchanged
      (syntax-rules ()
        ((_ Var ...)
         (%create-val "schlap.unchanged" (syntax-source-loc) Var ...))))

    ;; Choose Then if Cond holds and Else otherwise.
    (define-syntax ite
      (syntax-rules ()
        ((_ Cond Then Else)
         (%create-val "schlap.ite" (syntax-source-loc) Cond Then Else))))

    ;; Create the set of the elements.
    (define-syntax set-of
      (syntax-rules ()
        ((_ Element ...)
         (%create-val "schlap.set_of" (syntax-source-loc) Element ...))))

    ;; Create a tuple of the elements.
    (define-syntax tuple
      (syntax-rules ()
        ((_ Element ...)
         (%create-val "schlap.tuple" (syntax-source-loc) Element ...))))

    (define (tuple-ref* Loc Tuple Index)
      (unless (exact-integer? Index)
        (error-with-loc Loc "expecting an exact integer: {}" Index))
      (result
        (create-op* "schlap.tuple_get" Loc
                    (list (list "index"
                                (attr (number->string Index) "i64")))
                    (list (lift* Loc Tuple))
                    (list !schlap.unknown) '())))

    ;; Take the element of a tuple at a zero-based index.
    (define-syntax tuple-ref
      (syntax-rules ()
        ((_ Tuple Index)
         (tuple-ref* (syntax-source-loc) Tuple Index))))

    (define (fn* Loc VarLoc Domain BodyFn)
      (define DomainValue (lift* Loc Domain))
      (define Op
        (create-op* "schlap.fn" Loc '() (list DomainValue)
                    (list !schlap.unknown)
                    (list (list (list !schlap.unknown)
                                (lambda (Var)
                                  (yield* Loc (BodyFn Var)))))))
      (result Op))

    ;; Create the function from each (X ∈ S) to the body, one nested
    ;; function per binder.
    (define-syntax ↦
      (syntax-rules ()
        ((_ (B ...) Body1 BodyN ...)
         (%fn-binders (syntax-source-loc) (B ...) Body1 BodyN ...))))

    (define-syntax %fn-binders
      (syntax-rules (∈)
        ((_ L () Body ...)
         (let () Body ...))
        ((_ L ((X ∈ S) B ...) Body ...)
         (fn* L (syntax-source-loc X) S
              (lambda (X) (%fn-binders L (B ...) Body ...))))))

    ;; Apply a function to an argument.
    (define-syntax @
      (syntax-rules ()
        ((_ Fn Arg)
         (%create-val "schlap.apply" (syntax-source-loc) Fn Arg))))

    ;; Create the function that maps Arg to Value and is otherwise Fn.
    (define-syntax update
      (syntax-rules ()
        ((_ Fn Arg Value)
         (%create-val "schlap.update" (syntax-source-loc) Fn Arg Value))))
    )) ;; define-library
