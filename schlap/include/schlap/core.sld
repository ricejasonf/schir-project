(import (schir base))

;; Shared constructors for the operations of the Schlap dialect.
;;
;; Each constructor takes a source location first and creates one
;; operation with result type !schlap.unknown at the current insertion
;; point. Where an operand is expected an exact integer creates a
;; schlap.int, #t and #f create schlap.true and schlap.false, ∅
;; creates a schlap.empty_set and any other symbol a schlap.atom at the
;; location of the constructor, and a procedure is called with that
;; location to create the operand.
(define-library (schlap core)
  (export
    schlap-current-module
    schlap-module-init
    !schlap.unknown
    lift*
    atom*
    ∅ ⊤ ⊥
    ;; Formulas
    and* or* not* implies* iff* eq* ne* true* false*
    forall* exists* yield*
    ;; Integers
    int* add* sub* mul* neg* lt* le* gt* ge*
    ;; Sets
    member* not-member* union* intersect* subset* range* empty-set*
    )
  (import (schir base)
          (schir mlir))
  (begin
    (load-plugin "libSchlap.so")

    ;; Register the schlap dialect and its passes.
    (define schlap-init
      (load-builtin "schlap_init"))

    (define schlap-current-module
      (load-builtin "schlap_current_module"))

    (schlap-init)
    (load-dialect "schlap")

    ;; Create a module and set the insertion point to its body.
    (define (schlap-module-init name)
      (let ((ModuleOp
              (create-op "builtin.module"
                         (loc: 0)
                         (operands:)
                         (attributes: ("sym_name" name))
                         (result-types:)
                         (region: "body" () 0))))
        (at-block-begin (entry-block (get-region ModuleOp)))
        ModuleOp))

    (set! schlap-current-module
      (schlap-module-init "schlap_main"))

    (define !schlap.unknown (type "!schlap.unknown"))

    ;; Mark the empty set where a value is expected.
    (define ∅ '%schlap-empty-set)

    ;; Denote the formulas that always and never hold.
    (define ⊤ #t)
    (define ⊥ #f)

    ;; Create the operation for a Scheme value or return the value as is.
    (define (lift* Loc V)
      (cond
        ((exact-integer? V) (int* Loc V))
        ((eq? V #t) (true* Loc))
        ((eq? V #f) (false* Loc))
        ((eq? V ∅) (empty-set* Loc))
        ((symbol? V) (atom* Loc V))
        ((procedure? V) (lift* Loc (V Loc)))
        (else V)))

    ;; Create an operation with lifted operands and return its result.
    (define (%create-val OpName Loc . Operands)
      (define Values
        (map (lambda (V) (lift* Loc V)) Operands))
      (result
        (create-op OpName
                   (loc: Loc)
                   (operands: Values)
                   (attributes:)
                   (result-types: !schlap.unknown))))

    (define (int* Loc N)
      (unless (exact-integer? N)
        (error-with-loc Loc "expecting an exact integer: {}" N))
      (result
        (create-op "schlap.int"
                   (loc: Loc)
                   (operands:)
                   (attributes: ("value" (attr (number->string N) "i64")))
                   (result-types: !schlap.unknown))))

    (define (atom* Loc Name)
      (result
        (create-op "schlap.atom"
                   (loc: Loc)
                   (operands:)
                   (attributes: ("name" (string-attr Name)))
                   (result-types: !schlap.unknown))))

    (define (true* Loc) (%create-val "schlap.true" Loc))
    (define (false* Loc) (%create-val "schlap.false" Loc))
    (define (empty-set* Loc) (%create-val "schlap.empty_set" Loc))

    (define (and* Loc A B) (%create-val "schlap.and" Loc A B))
    (define (or* Loc A B) (%create-val "schlap.or" Loc A B))
    (define (not* Loc A) (%create-val "schlap.not" Loc A))
    (define (implies* Loc A B) (%create-val "schlap.implies" Loc A B))
    (define (iff* Loc A B) (%create-val "schlap.iff" Loc A B))
    (define (eq* Loc A B) (%create-val "schlap.eq" Loc A B))
    (define (ne* Loc A B) (%create-val "schlap.ne" Loc A B))

    (define (add* Loc A B) (%create-val "schlap.add" Loc A B))
    (define (sub* Loc A B) (%create-val "schlap.sub" Loc A B))
    (define (mul* Loc A B) (%create-val "schlap.mul" Loc A B))
    (define (neg* Loc A) (%create-val "schlap.neg" Loc A))
    (define (lt* Loc A B) (%create-val "schlap.lt" Loc A B))
    (define (le* Loc A B) (%create-val "schlap.le" Loc A B))
    (define (gt* Loc A B) (%create-val "schlap.gt" Loc A B))
    (define (ge* Loc A B) (%create-val "schlap.ge" Loc A B))

    (define (member* Loc A B) (%create-val "schlap.member" Loc A B))
    (define (not-member* Loc A B) (%create-val "schlap.not_member" Loc A B))
    (define (union* Loc A B) (%create-val "schlap.union" Loc A B))
    (define (intersect* Loc A B) (%create-val "schlap.intersect" Loc A B))
    (define (subset* Loc A B) (%create-val "schlap.subset" Loc A B))
    (define (range* Loc A B) (%create-val "schlap.range" Loc A B))

    ;; Yield the lifted value V from the current region.
    (define (yield* Loc V)
      (define Value (lift* Loc V))
      (create-op "schlap.yield"
                 (loc: Loc)
                 (operands: Value)
                 (attributes:)
                 (result-types:))
      (if #f #f))

    ;; Create a binder whose body is the result of BodyFn
    ;; applied to the bound variable at VarLoc.
    (define (%create-binder OpName Loc VarLoc Domain BodyFn)
      (define DomainValue (lift* Loc Domain))
      (define Op
        (create-op OpName
                   (loc: Loc)
                   (operands: DomainValue)
                   (attributes:)
                   (result-types: !schlap.unknown)
                   (region: "body" () 0)))
      (with-builder (lambda ()
        (define Block (entry-block Op))
        (define Var (add-argument Block !schlap.unknown VarLoc))
        (at-block-begin Block)
        (yield* Loc (BodyFn Var))))
      (result Op))

    (define (forall* Loc VarLoc Domain BodyFn)
      (%create-binder "schlap.forall" Loc VarLoc Domain BodyFn))

    (define (exists* Loc VarLoc Domain BodyFn)
      (%create-binder "schlap.exists" Loc VarLoc Domain BodyFn))
    )) ;; define-library
