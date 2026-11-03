(import (schir builtins))

(define-library (schir base record)
  (export
    define-record-type)
  (import (schir builtins)
          (schir base r7rs-syntax)
          (schir base list))
  (begin
    ;; Get the record field index of Field in the list of Fields.
    (define (%record-field-index Name Fields Field)
      (let Loop ((Fields Fields)
                 (Index 0))
        (cond
          ((null? Fields)
           (error "record type {} has no field {}" Name Field))
          ((eq? (car Fields) Field)
           Index)
          (else
            (Loop (cdr Fields) (+ Index 1))))))

    ;; Create a constructor that initializes the given fields in order.
    (define (%record-constructor Type Name Fields Args)
      (define Size (length Fields))
      (define Arity (length Args))
      (define Indices
        (map (lambda (Arg) (%record-field-index Name Fields Arg)) Args))
      (lambda Vals
        (unless (eqv? (length Vals) Arity)
          (error "record constructor for {} expects {} arguments: {}"
                 Name Arity Vals))
        (let ((Record (%make-record Type Size)))
          (for-each (lambda (Index Val)
                      (%record-set! Record Index Val))
                    Indices Vals)
          Record)))

    (define (%record-predicate Type)
      (lambda (Obj)
        (%record? Obj Type)))

    (define (%record-check Type Name Obj)
      (unless (%record? Obj Type)
        (error "expecting record of type {}: {}" Name Obj)))

    (define (%record-accessor Type Name Fields Field)
      (define Index (%record-field-index Name Fields Field))
      (lambda (Obj)
        (%record-check Type Name Obj)
        (%record-ref Obj Index)))

    (define (%record-modifier Type Name Fields Field)
      (define Index (%record-field-index Name Fields Field))
      (lambda (Obj Val)
        (%record-check Type Name Obj)
        (%record-set! Obj Index Val)))

    (define-syntax %define-record-field
      (syntax-rules ()
        ((%define-record-field Type Fields (Field Accessor))
         (define Accessor (%record-accessor Type 'Type Fields 'Field)))
        ((%define-record-field Type Fields (Field Accessor Modifier))
         (begin
           (define Accessor (%record-accessor Type 'Type Fields 'Field))
           (define Modifier (%record-modifier Type 'Type Fields 'Field))))))

    (define-syntax %define-record-fields
      (syntax-rules ()
        ((%define-record-fields Type Fields FieldSpec ...)
         (begin
           (%define-record-field Type 'Fields FieldSpec)
           ...))))

    ;; Bind Type to a symbol that uniquely identifies the record type.
    (define-syntax define-record-type
      (syntax-rules ()
        ((define-record-type Type (Constructor Arg ...) Pred
                            (Field . Procs) ...)
         (begin
           (define Type (%record-type-id Type))
           (define Constructor
             (%record-constructor Type 'Type '(Field ...) '(Arg ...)))
           (define Pred (%record-predicate Type))
           (%define-record-fields Type (Field ...) (Field . Procs) ...)))))
    ) ; end begin
  )
