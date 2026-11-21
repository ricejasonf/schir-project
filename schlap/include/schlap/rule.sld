(import (schir base))

;; Define rules about the IR of functions and check them.
;;
;; A rule is a schlap.rule operation in the Schlap module. Its body is an
;; ordinary Scheme body like that of define-spec: variables and init come
;; from (schlap base), and on-mutate and on-use state what happens when an
;; operation of the function mutates or uses a value. In an event, root,
;; read-from, owned-by and child-stable refer to the structure of the
;; function, and require states a formula that holds before the event.
;;
;; Import (schir base) with (except (schir base) < > + - *) beside
;; (schlap base) and (schlap infix).
(define-library (schlap rule)
  (export
    define-rule on-mutate on-use require root read-from owned-by
    child-stable
    check-rule check-rule*)
  (import (except (schir base) < > + - *)
          (schir mlir)
          (schlap core))
  (begin
    (define %schlap-check-rule
      (load-builtin "schlap_check_rule"))

    ;; Mark the end of an event that does not change the state.
    (define %no-action '%schlap-no-action)

    (define (rule* Loc Name BodyFn)
      (define Op #f)
      (with-module-builder schlap-current-module
        (lambda ()
          (set! Op
            (create-op* "schlap.rule" Loc
                        (list (list "sym_name" (string-attr Name)))
                        '() '() (list (list '() (lambda () #f)))))))
      (with-builder
        (lambda ()
          (at-block-begin (entry-block Op))
          (BodyFn)))
      Op)

    ;; Define Name as a rule whose body is Body.
    (define-syntax define-rule
      (syntax-rules ()
        ((_ Name Body ...)
         (define Name
           (rule* (syntax-source-loc Name) 'Name
                  (lambda () Body ... (if #f #f)))))))

    ;; Create an event whose body is the result of BodyFn applied to the
    ;; value of the event.
    (define (event* OpName Loc BodyFn)
      (create-op* OpName Loc '() '() '()
                  (list (list (list !schlap.unknown)
                              (lambda (V)
                                (define Action (BodyFn V))
                                (if (eq? Action %no-action)
                                  (create-op* "schlap.done" Loc
                                              '() '() '() '())
                                  (yield* Loc Action))))))
      (if #f #f))

    ;; State what happens when an operation mutates the value V.
    (define-syntax on-mutate
      (syntax-rules ()
        ((_ (V) Body1 BodyN ...)
         (event* "schlap.on_mutate" (syntax-source-loc)
                 (lambda (V) Body1 BodyN ...)))))

    ;; State what happens when an operation uses the value V.
    (define-syntax on-use
      (syntax-rules ()
        ((_ (V) Body1 BodyN ...)
         (event* "schlap.on_use" (syntax-source-loc)
                 (lambda (V) Body1 BodyN ...)))))

    (define (require* Loc Formula)
      (create-op* "schlap.require" Loc '() (list (lift* Loc Formula))
                  '() '())
      %no-action)

    ;; Require a formula to hold before the event happens.
    (define-syntax require
      (syntax-rules ()
        ((_ Formula)
         (require* (syntax-source-loc) Formula))))

    ;; Create an operation of one lifted operand and return its result.
    (define (%create-val OpName Loc Operand)
      (result
        (create-op* OpName Loc '() (list (lift* Loc Operand))
                    (list !schlap.unknown) '())))

    ;; Take the value that V is read from, followed back to a value that
    ;; is not read from another.
    (define-syntax root
      (syntax-rules ()
        ((_ V)
         (%create-val "schlap.root" (syntax-source-loc) V))))

    ;; Take the set of the values read from R that are defined before the
    ;; event.
    (define-syntax read-from
      (syntax-rules ()
        ((_ R)
         (%create-val "schlap.read_from" (syntax-source-loc) R))))

    ;; Take the set of the values that V may own, as a part of its object,
    ;; and are defined before the event.
    (define-syntax owned-by
      (syntax-rules ()
        ((_ V)
         (%create-val "schlap.owned_by" (syntax-source-loc) V))))

    ;; State that the event of V is a mutation that leaves each value V
    ;; owns valid.
    (define-syntax child-stable
      (syntax-rules ()
        ((_ V)
         (%create-val "schlap.child_stable" (syntax-source-loc) V))))

    ;; Check Rule on the function Fn and return the number of distinct
    ;; states. The option is 'state-limit N.
    (define (check-rule* Loc Rule Fn . Options)
      (define StateLimit 1000000)
      (let loop ((Opts Options))
        (cond
          ((null? Opts) #t)
          ((or (null? (cdr Opts)) (not (symbol? (car Opts))))
           (error-with-loc Loc "expecting option and value: {}" Opts))
          ((eq? (car Opts) 'state-limit)
           (set! StateLimit (cadr Opts))
           (loop (cddr Opts)))
          (else
           (error-with-loc Loc "unknown check option: {}" (car Opts)))))
      (%schlap-check-rule Rule Fn Loc StateLimit))

    ;; Check a rule on a function.
    (define-syntax check-rule
      (syntax-rules ()
        ((_ Rule Fn Option ...)
         (check-rule* (syntax-source-loc) Rule Fn Option ...))))
    )) ;; define-library
