(import (schir builtins))

(define-library (schir mlir)
  (export
    create-op
    with-module-builder
    ; (schir mlir builtins)
    create-top-module
    old-create-op
    current-builder
    set-pass-debug-mode
    get-region
    entry-block
    add-argument
    result-values
    result
    at-block-begin
    at-block-end
    block-op
    op-next
    parent-op
    set-insertion-point
    set-insertion-after
    type
    %function-type
    function-type-results
    function-type-inputs
    attr
    type-attr
    value-attr
    string-attr
    flat-symbolref-attr
    float-attr
    with-new-context
    with-builder
    load-dialect
    verify
    module-lookup
    value?
    )
  (import (schir base)
          (schir mlir builtins))
  (begin
    (define (init-regions Op BlockArgLocList BlockArgTypesList UserFns)
      (define Is (range (length BlockArgTypesList)))
      (define (InitRegion RegionIndex BlockArgTypes BlockArgLocs UserFn)
        (with-builder
          (lambda ()
            (define Region (get-region Op RegionIndex))
            (define Block (entry-block Region))
            (define Args
              (let ()
                (define (Proc BlockArgType BlockArgLoc)
                  (add-argument Block BlockArgType BlockArgLoc))
                (map Proc BlockArgTypes BlockArgLocs)))
            (at-block-begin Block)
            (apply UserFn Args))))
      (map InitRegion Is BlockArgTypesList BlockArgLocList UserFns))

    (define-syntax create-op
      (syntax-rules (: loc: attributes: operands: result-types: region:)
        ((create-op Name
                    (loc: Loc)
                    (operands: Operands ...)
                    (attributes: (AttrName Attr) ...))
         (%create-op
           Name
           (source-loc Loc (syntax-source-loc Name))
           #((list 'AttrName Attr) ...)
           #(Operands ...)
           0 ; Regions
           '() ; ResultTypes
           #() ; Successors
           ))
        ((create-op Name
                    (loc: Loc)
                    (operands: Operands ...)
                    (attributes: (AttrName Attr) ...)
                    (result-types: ResultTypes ...)
                    (region: RegionName ((BlockArg : BlockArgType) ...)
                             RegionBody1 RegionBodyN ...) ...)
         (let ((Op (%create-op
                     Name
                     (source-loc Loc (syntax-source-loc Name))
                     #((list 'AttrName Attr) ...)
                     #(Operands ...)
                     (length '(RegionName ...))
                     #(ResultTypes ...)
                     #() ; Successors
                     ))
               (BlockArgLocList
                 (list (list (syntax-source-loc BlockArg) ...) ...))
               (BlockArgTypesList (list (list BlockArgType ...) ...))
               (UserFns (list (lambda (BlockArg ...)
                                RegionBody1 RegionBodyN ...) ...)))
           (init-regions Op BlockArgLocList BlockArgTypesList UserFns)
           Op))
        ))

    ;; Create new builder context for inserting module level operations.
    (define (with-module-builder ModuleOp Thunk)
      (with-builder
        (lambda ()
          (at-block-end (entry-block ModuleOp))
          (Thunk))))

    (define (set-pass-debug-mode V)
      (set! %is-pass-debug-mode
        (not (not V))))
    ));

