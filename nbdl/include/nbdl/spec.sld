(import (schir base))

(define-library (nbdl spec)
  (import (schir base)
          (schir mlir)
          (schir mlir all-passes)
          (schir clang))
  (begin
    ;; Note that the %match functions in this implementation
    ;; use a CPS style where callbacks can be called multiple
    ;; times to generate code for the regions of every combination
    ;; of possible branches.

    (load-plugin "libNbdl.so")
    (define translate-cpp
      (load-builtin "nbdl_spec_translate_cpp"))
    (define close-previous-scope
      (load-builtin "nbdl_spec_close_previous_scope"))
    (define register-nbdl-dialect
      (load-builtin "nbdl_spec_register_nbdl_dialect"))
    (define get-store-alts
      (load-builtin "nbdl_spec_get_store_alts"))
    (define !nbdl.store
      (load-builtin "nbdl_spec_create_store_type"))
    (define get-member-name
      (load-builtin "nbdl_get_member_name"))
    (define nbdl_run_flatten_pass
      (load-builtin "nbdl_run_flatten_pass"))

    ;; "Cpp" module will translate to c++ via translate-cpp.
    (define main-module (create-top-module "nbdl_spec_module_cpp"))

    (register-nbdl-dialect)
    (load-dialect "func")
    (load-dialect "schir")
    (load-dialect "nbdl")

    ;; Thunk should return a new top level operation using the provided
    ;; module builder. The new operation is immediately translated to to C++
    ;; when its name is in the list of export-cpp names.
    ;; This is done immediately to make the C++ type available for
    ;; introspection when making subsequent operations.
    (define (top-level-op Name Thunk)
      (with-module-builder
        main-module
        (lambda ()
          (define TopLevelOp (Thunk))
          (define Loc (source-loc TopLevelOp))
          ;; The verify pass may also raise a more specific error.
          (verify TopLevelOp)
          ;; Infer types and simplify operations.
          (nbdl_run_flatten_pass TopLevelOp
                                 current-schir-clang)
          ;; Emit c++ when exported.
          (when (memq Name export-cpp-names)
            (begin
              (translate-cpp TopLevelOp lexer-writer)
              (flush-tokens)))
          TopLevelOp)))

    (define !nbdl.member_name (type "!nbdl.member_name"))
    (define !nbdl.func_name (type "!nbdl.func_name"))
    (define !nbdl.unit (type "!nbdl.unit"))
    (define i32 (type "i32"))
    (define f32 (type "f32"))

    ;; Not used types
    ; (define !nbdl.tag (type "!nbdl.tag")) ; Not used.
    ; (define !nbdl.empty (type "!nbdl.empty"))

    (define %probe-id 0)
    (define (make-probe-name)
      (set! %probe-id (+ 1 %probe-id))
      (string-append
        "nbdl::detail::probe<"
        (number->string %probe-id)
        ">::apply"))

    ; Return a list of c++ types representing the
    ; alternatives of a store when calling match
    ; with a key. Use '() for the "unit" key.
    (define (reflect-match Loc StoreTypename KeyTypename)
      ;(define StoreTypename "int")
      (define ProbeName (make-probe-name))
      (define KeyArgClause
        (if (null? KeyTypename)
          ""
          (string-append "nbdl::detail::declval<" KeyTypename ">(), ")))
      (define FullExpr
        (string-append
          "nbdl::match(nbdl::detail::declval<" StoreTypename ">(), "
          KeyArgClause
          "[](auto&& ... args) -> void { (void)"
          ProbeName "<std::remove_cvref_t<decltype(args)>...>(); })"))
      (define Result
        (template-probe
          Loc
          ProbeName
          FullExpr))
      ; Unnest the alternatives.
      (apply append Result)
      )

    ;; Return single alternative of a store value
    ;; or false if there is not exacly one.
    (define (get-single-alternative Value)
      (define Alts (get-store-alts Value))
      (if (and (pair? Alts) (eq? (cdr Alts) '()))
        (car Alts)
        #f))

    (define %nbdl-expr '%nbdl-expr)
    ;; Create a thunk that should receive a location and callback
    ;; to resolve a value once its dependencies are resolved.
    ;; (e.g. arguments to visit)
    ;; Additionally, an optional MatchImpl may be provided
    ;; to specify how the result is matched given a procedure
    ;; taking the following arguments:
    ;;   MatchImpl: (Loc Store Key Fn) => ()
    ;; where Store is the Expr sans the MatchImpl.
    (define-syntax %expr
      (syntax-rules ()
        ;; Thunk: (Loc Fn) -> Store
        ((%expr Loc Thunk)
         (list %nbdl-expr Loc Thunk #f))
        ((%expr Loc Thunk MatchImpl)
         (list %nbdl-expr Loc Thunk MatchImpl))))

    ;; Handle the typical %expr use case of handling a single expr.
    (define-syntax %single-expr
      (syntax-rules ()
        ((%single-expr Input)
         (%single-expr (syntax-source-loc Input) Input))
        ((%single-expr Loc Input)
         (%expr Loc
                (lambda (Loc_ Fn)
                  (%match-expr Loc_ Input Fn))))))

    ;; Handle single expr that can include member names
    ;; and scheme procedures that take resolved values.
    (define-syntax %single-expr+
      (syntax-rules ()
        ((%single-expr+ Input)
         (%single-expr+ (syntax-source-loc Input) Input))
        ((%single-expr+ Loc Input)
         (%expr Loc
                (lambda (Loc_ Fn)
                  (%match-expr+ Loc_ Input Fn))))))

    (define (expr? Arg)
      (and (pair? Arg) (eqv? %nbdl-expr (car Arg))))

    ;; Invoke the Thunk created with %expr
    (define (%invoke-expr Expr Fn)
      (define-values (Tag Loc Thunk MatchImpl)
        (if (and (pair? Expr)
                 (eq? %nbdl-expr (car Expr)))
          (apply values Expr)
            ;(cadr Expr)
            ;(car (cddr Expr)))
          (error "expecting %expr: {}" Expr)))
      ; TODO If MatchImpl is set, maybe create a new type?
      (when MatchImpl
        (error "MatchImpl is not unwrapped: {}" Expr))
      (Thunk Loc Fn))

    ; Maybe lift to a LiteralOp or ConstexprOp.
    (define (maybe-build-expr Loc Arg)
      (cond
        ((symbol? Arg)
          (build-constexpr Loc Arg))
        ((number? Arg)
          (build-literal Loc (attr (number->string Arg) i32)
                         (!nbdl.store "int32_t")))
        ((string? Arg)
          (build-literal Loc (string-attr Arg)
                         (!nbdl.store "std::string_view")))
        (else Arg)))

    ;; Maybe lift to a LiteralOp, ConstexprOp, or MemberNameOp.
    (define (maybe-build-expr+ Loc Arg)
      (if (member-name-literal? Arg)
        (build-member-name Loc Arg)
        (maybe-build-expr Loc Arg)))

    (define (%match-expr-aux Loc Expr Fn)
      (cond
        ((value? Expr)
         (Fn Expr))
        ((expr? Expr)
         (%invoke-expr Expr Fn))
        ((path? Expr)
         (%match-path-spec Expr Fn))
        ((named-fn? Expr)
         (Fn (build-func-name Expr)))
        ((discarded-loc Expr)
         => (lambda (DiscardLoc)
              (error-with-loc Loc "value is discarded and cannot be used"
                     (error-note "value discarded here" DiscardLoc))))
        (else (error-with-loc Loc "unable to resolve value: {}" Expr)))
      ; Return a "discarded" value to provide a better error message.
      ; This prevents the user from passing around out of scope SSA values.
      ; FIXME nbdl.scope was created to prevent this,
      ;       but it seems needlessly restrictive for expressions
      ;       that have results like `visit`.
      ;       Or we stick with `visit` result being discard when not
      ;       operands to another `visit`. Maybe this was to prevent
      ;       combinational explosions.
      (list %nbdl-discard Loc))

    (define (%match-expr Loc ExprArg Fn)
      ;; Disallow expr+ extra cases.
      (cond
        ((procedure? ExprArg)
         (error-with-loc Loc "unexpected procedure"))
        ((member-name-literal? ExprArg)
         (error-with-loc Loc "unexpected member name: {}" ExprArg))
        (else
          (let ((Expr (maybe-build-expr Loc ExprArg)))
            (%match-expr-aux Loc Expr Fn)))))

    (define (%match-expr+ Loc ExprArg Fn)
      (define Expr (maybe-build-expr+ Loc ExprArg))
      (%match-expr-aux Loc Expr Fn))

    (define (build-unit)
      (result
        (create-op "nbdl.unit"
          (loc: 0)
          (operands:)
          (attributes:)
          (result-types: !nbdl.unit))))

    (define (build-literal Loc Arg StoreT)
      (result
        (create-op "nbdl.literal"
          (loc: Loc)
          (operands:)
          (attributes: ("value" Arg))
          (result-types: StoreT))))

    (define (build-constexpr Loc ExprStr)
      (define StoreT
        (!nbdl.store (expr->type ExprStr)))
      (when (member-name-literal? ExprStr)
        (error "unexpected member name: {}" ExprStr))
      (result
        (create-op "nbdl.constexpr"
          (loc: Loc)
          (operands:)
          (attributes: ("expr" (string-attr ExprStr)))
          (result-types: StoreT))))

    ; Build a key for store-compose.
    (define (build-store-key Loc Key)
      (cond
        ((value? Key) Key)
        ((member-name-literal? Key)
           (build-member-name Loc Key))
        (else (build-constexpr Loc Key))))

    ;; Expects name begins with a .
    (define (build-member-name Loc Name)
      (define StrippedName
        (begin
          (unless (member-name-literal? Name)
            (error "expecting member name: {}" Name))
          (string-copy Name 1)))
      (result
        (create-op "nbdl.member_name"
          (loc: Loc)
          (operands:)
          (attributes: ("name" (string-attr StrippedName)))
          (result-types: !nbdl.member_name))))

    (define (build-cont ResultArg)
      (create-op "nbdl.cont"
        (loc: 0)
        (operands: ResultArg)
        (attributes:)
        (result-types:)
        ))

    (define (build-store Loc Typename InitArgs)
      (result
        (create-op
          "nbdl.store"
          (loc: Loc)
          (operands: InitArgs)
          ; TODO Mangle Typename
          (attributes: ("name" (flat-symbolref-attr Typename)))
          (result-types: (!nbdl.store (parse-type Typename)))
          )))

    (define (store-aux Loc Typename InitArgExprs)
      (%expr
        Loc
        (lambda (Loc Fn)
          (%match-results
            InitArgExprs
            (lambda (InitArgs)
              (Fn (build-store Loc Typename InitArgs)))))))

    (define-syntax store
      (syntax-rules (init-args:)
        ((store Typename)
         (store Typename (init-args:)))
        ((store Typename (init-args: InitArgN ...))
         (store-aux
           (syntax-source-loc Typename)
           Typename
           (list (%single-expr InitArgN) ...)))))

    (define (build-store-compose Loc Key Store ParentStore)
      (define KeyVal
        (build-store-key Loc Key))
      (result
        (create-op
          "nbdl.store_compose"
          (loc: Loc)
          (operands: KeyVal Store ParentStore)
          (attributes:)
          (result-types: (!nbdl.store)))))

    (define (store-compose-aux Loc KeyExpr StoreExpr ParentStoreExpr)
      (%expr
        Loc
        (lambda (Loc Fn)
          (%match-results
            (list KeyExpr StoreExpr ParentStoreExpr)
            (lambda (Results)
              (define-values (Key Store ParentStore)
                (apply values Results))
              (Fn (build-store-compose Loc Key Store ParentStore)))))))

    (define-syntax store-compose
      (syntax-rules ()
        ((store-compose Key Store ParentStore)
         ((store-compose Key Store) ParentStore))
        ((store-compose Key Store)
         (lambda (ParentStore)
           (store-compose-aux
             (syntax-source-loc Key)
             (%single-expr+ Key)
             (%single-expr Store)
             ParentStore)))))

    (define (build-variant Loc Stores)
      (define ResultT
        (apply !nbdl.store (map get-type Stores)))
      (result
        (create-op
          "nbdl.variant"
          (loc: Loc)
          (operands: Stores)
          (attributes:)
          (result-types: ResultT))))

    (define (variant-aux Loc StoreExprs)
      (%expr
        Loc
        (lambda (Loc Fn)
          (%match-results
            StoreExprs
            (lambda (Stores)
              (Fn (build-variant Loc Stores)))))))

    (define-syntax variant
      (syntax-rules ()
        ((variant Store1 StoreN ...)
         (variant-aux
           (syntax-source-loc Store1)
           (list
             (%single-expr Store1)
             (%single-expr StoreN) ...)))))

    (define (define-store-aux Loc BodyThunk)
      (define Parent (build-unit))
      (define (ProcessBody BodyEl)
        (define (SetParent Store)
          (set! Parent Store))
        (cond
          ; StoreFunctional
          ((procedure? BodyEl)
           (%invoke-expr (BodyEl Parent) SetParent))
          ; StoreExpr
          ((expr? BodyEl)
           (%invoke-expr BodyEl SetParent))
          ; Store (mlir.value)
          ((value? !nbdl.unit Parent)
           (SetParent BodyEl))
          (else
            (error "expecting store: {}" BodyEl))))
        (BodyThunk ProcessBody)
        (create-op "nbdl.cont"
                   (loc: Loc)
                   (operands: Parent)
                   (attributes:)
                   (result-types:)))

    ; A StoreFunctional either a Store (operation) or a
    ;   map: ParentStore -> NewStore.
    ; These are created using syntax like `store` or `store-compose`.
    (define-syntax define-store
      (syntax-rules ()
        ((define-store Name (InitParams ...) StoreFunctionalN ...)
         (begin
           (define Name
             (let ((QualName (namespace-prefix 'Name)))
               (top-level-op
                 QualName
                 (lambda ()
                   (define Loc (syntax-source-loc Name))
                   (create-op
                     "nbdl.define_store"
                     (loc: Loc)
                     (operands:)
                     (attributes: ("sym_name" (string-attr QualName)))
                     (result-types:)
                     (region: "body" ((InitParams : (!nbdl.store)) ...)
                              (define-store-aux
                                Loc
                                (lambda (ProcessBody)
                                  ;; Ensure nonempty lambda.
                                  (ProcessBody StoreFunctionalN) ... #t)
                                )))))
               QualName))))))

    ;; For now, this is just an alternative interface to define-store.
    ;; The idea was to encapsulate a root node in the state graph
    ;; but the benefit is not apparent.
    (define-syntax define-context
      (syntax-rules (member: init-args:)
        ((define-context Name (Formals ...)
            (member: Key1 Typename1 (init-args: InitArgs1N ...))
            (member: KeyN TypenameN (init-args: InitArgsNN ...)) ...)
         (define-store Name (Formals ...)
           (store-compose Key1 (store Typename1 (init-args: InitArgs1N ...)))
           (store-compose KeyN (store TypenameN (init-args: InitArgsNN ...)))
           ...
           ))))

    (define %named-fn '%named-fn)

    ;; A 'named-fn' can be used in an expr+
    (define (make-named-fn SymbolName FuncOp)
      (list %named-fn SymbolName FuncOp))

    (define (named-fn? Value)
      (and (pair? Value)
           (eq? %named-fn (car Value))))

    (define (build-func-name NamedFn)
      (define-values (_ Name FuncOp)
        (apply values NamedFn))
      (result (create-op
                "nbdl.func_name"
                (loc: (source-loc FuncOp))
                (operands:)
                (attributes:
                  ("name" (flat-symbolref-attr Name)))
                (result-types: !nbdl.func_name))))

    ;; Define a function to receive a matched set of parameters.
    ;; Each path node should be of the format:
    ;;  (%Kind Loc Args...)
    ;; or specifically for nested pathspecs:
    ;;  (%nbdl-path PathNodes...)
    (define-syntax match-params-fn
      (syntax-rules ()
        ((match-params-fn Name (Stores ... Fn) Body ...)
         (define Name
           (let ((QualName (namespace-prefix 'Name)))
             (make-named-fn
               QualName
               (top-level-op
                 QualName
                 (lambda ()
                   (create-op
                     "func.func"
                     (loc: (syntax-source-loc Name))
                     (operands:)
                     (attributes:
                       ("sym_name" (string-attr QualName))
                       ("function_type"
                        (type-attr
                          (%function-type
                            (make-vector
                              (length '(Stores ... Fn))
                              (!nbdl.store))
                            #()))))
                     (result-types:)
                     (region: "body" ((Stores : (!nbdl.store))
                                      ...
                                      (Fn : (!nbdl.store)))
                              Body ...))))))))))

    ;; Transform each element in a list calling ParamsFn with the results.
    ;; MapFn must take a single argument and a callback.
    (define (%map-params MapFn Params ParamsFn)
      (let Loop ((ParamValsRev '()) ; Reverse ordered
                 (CurParam (car Params))
                 (Rest (cdr Params)))
        (define (NextFn ParamVal)
          (define NewParamValsRev
            (cons ParamVal ParamValsRev))
          (cond
            ((pair? Rest)
              (Loop NewParamValsRev
                    (car Rest)
                    (cdr Rest)))
            ((null? Rest)
              (ParamsFn (reverse NewParamValsRev)))
            (else (error "expecting proper list" Rest))))
        (MapFn CurParam NextFn)))

    ; ParamsSpec is a list of PathSpecs
    ; ParamsFn is the callback taking the list of results.
    (define (%match-params-spec ParamsSpec ParamsFn)
      (if (null? ParamsSpec)
        (ParamsFn '())
        (%map-params %match-path-spec ParamsSpec ParamsFn)))

    (define (%match-path-spec PathSpec Fn)
      (cond
        ((value? PathSpec)
         (Fn PathSpec))
        ((expr? PathSpec)
         (%invoke-expr PathSpec Fn))
        ; Procedures pass through (via expr+).
        ((procedure? PathSpec)
         PathSpec)
        ((and (pair? PathSpec)
              (eqv? '%nbdl-path (car PathSpec)))
         (let ((RootStore (cadr PathSpec))
               (PathNodes (cddr PathSpec)))
          (cond
            ((and (value? RootStore)
                  (pair? PathNodes))
              (%match-path-spec-rec RootStore PathNodes Fn))
            ((value? RootStore)
              (Fn RootStore))
            (else
              (error "expecting a root store object in pathspec: {}"
                     PathSpec)))))
        (else (error "expecting nbdl pathspec: {}" PathSpec))))

    (define (%match-path-spec-rec Store PathNodes Fn)
      (let Loop ((Loc (source-loc PathNodes))
                 (PathNode (car PathNodes))
                 (Rest (cdr PathNodes))
                 (CurStore Store))
        (define (NextFn StoreResult)
          (cond
            ((pair? Rest)
             (Loop (source-loc Rest)
                   (car Rest)
                   (cdr Rest)
                   StoreResult))
            ; Finish by match with unit-key to "unwrap" store.
            ((null? Rest)
             (%match-unit Loc StoreResult Fn))
            (else (error "expecting proper list"))))
        (%match-path-node CurStore Loc PathNode NextFn)))

    ;; Detect if match is the identity operation for a store
    ;; so we can not generate a match operation for it.
    ;; It is a single known type with no match_impl.
    (define (%match-is-identity? Loc Store)
      (define StoreAlts (get-store-alts Store))
      (cond
        ((and (pair? StoreAlts) (null? (cdr StoreAlts)))
          (let ()
            (define StoreT (car StoreAlts))
            (define Expr
              (string-append "nbdl::Store<" StoreT ">"))
            (define Storex
              (expr-eval Loc Expr))
            (not Storex)))
        (else #f)))

    ;; Match a store with unit-key unless
    ;; it would be the identity operation.
    (define (%match-unit Loc Store Fn)
      (if (%match-is-identity? Loc Store)
        (Fn Store)
        (%match-key Loc Store '() Fn)))

    ;; We have mlir.values for both Store and Key
    (define (%match-key Loc Store Key Fn)
      ; "Alt" here means a c++ type written as symbol.
      (define StoreAlts (get-store-alts Store))
      (define KeyAlts (get-store-alts Key))
      (define MatchedAlts
        (cond
          ((and StoreAlts KeyAlts)
           (let ()
             (define (ReflectAlts StoreAlt)
               (lambda (KeyAlt)
                 (reflect-match Loc StoreAlt KeyAlt)))
             (apply append (map apply (map ReflectAlts StoreAlts) KeyAlts))))
          ((and StoreAlts (null? Key))
           (let ()
             (define (ReflectAlts StoreAlt)
                 (reflect-match Loc StoreAlt '()))
             (apply append (map ReflectAlts StoreAlts))))
           (else '())))
      (define StoreT
        (apply !nbdl.store MatchedAlts))
      (create-op "nbdl.match"
        (loc: Loc)
        (operands: Store Key)
        (attributes:)
        (result-types:)
        (region: "overloads" ((ResolvedStore : StoreT))
          (Fn ResolvedStore))))

    (define (member-name-literal? PathNode)
      (and (symbol? PathNode)
           (eq? (string-ref PathNode 0) #\.)))

    (define (%match-path-node Store Loc PathNode Fn)
      (close-previous-scope)
      (let ((PathNode
              (maybe-build-expr+ Loc PathNode)))
        (cond
          ; TODO A match-params-fn should lift a proc to take Fn.
          ;; A scheme procedure is akey where
          ;; its `get` implementation is determined by
          ; invoking it.
          ((procedure? PathNode)
           (Fn (PathNode Store))) ; TODO Test this.
          ; Member name is the only key kind where nbdl.get is required
          ; but we have to apply the identity first to unwrap the store.
          ; (Which means the member name is applied to all alternatives.)
          ((value? !nbdl.member_name PathNode)
            (%match-unit Loc Store
              (lambda (MatchedStore)
                (define MemberStore
                  (build-node-get MatchedStore Loc
                                  PathNode))
                (Fn MemberStore))))
          ; Any other resolved mlir.value.
          ((value? PathNode)
            (%match-key Loc Store PathNode Fn))
          ; Match a nested PathSpec then continue.
          ((path? PathNode)
            (%match-path-spec PathNode
              (lambda (KeyVal)
                (%match-path-node Store Loc KeyVal Fn))))
          (else (error "unsupported path node kind: {}" PathNode))
          )))

    (define (build-node-get Store Loc KeyVal)
      ;; Infer type for single alternatives only.
      (define StoreT (get-single-alternative Store))
      (define KeyT (get-single-alternative KeyVal))
      (define KeyValMemberName
        (if (value? !nbdl.member_name KeyVal)
          (get-member-name KeyVal)
          #f))
      ;; Try to get the alternative from the C++ expr type.
      (define Expr
        (cond
          ((and StoreT KeyValMemberName)
           (string-append
             "nbdl::detail::declval<" StoreT ">()."
             KeyValMemberName))
          ((and StoreT KeyT)
            (string-append
              "nbdl::get(nbdl::detail::declval<" StoreT ">,"
              "          nbdl::detail::declval<" KeyVal ">"))

          (else #f)))
      (define ExprT
        (and (string? Expr)
             (expr->type Expr)))
      (define ResultType
        (if ExprT
          (!nbdl.store (expr->type Expr))
          (!nbdl.store)))
      (define Op
        (create-op "nbdl.get"
          (loc: Loc)
          (operands: Store KeyVal)
          (attributes:)
          (result-types: ResultType)))
      (result Op))

    (define (build-resolve-params Loc FnVal ParamVals)
      (unless (or (pair? ParamVals)
                  (null? ParamVals))
        (error-with-loc Loc "expecting list of params: {}" ParamVals))
      (let ()
        (define Result
          (result
            (create-op "nbdl.visit"
                       (loc: Loc)
                       (operands: FnVal ParamVals)
                       (attributes:)
                       (result-types: !nbdl.unit))))
        (build-discard Loc Result)))

    (define (path? obj)
      (and (pair? obj)
           (eqv? (car obj) '%nbdl-path)))

    ;; Create a new path appending keys to the input path.
    (define-syntax get
      (syntax-rules ()
        ((get path key ...)
         (cond
          ((value? path)
            (append (list '%nbdl-path path)
                    (source-cons key '() (syntax-source-loc key)) ...))
          ((path? path)
            (append path
                    (source-cons key '() (syntax-source-loc key)) ...))
          (else (error "invalid path object: {}" path))
          ))
        ))

    ;; Apply a "Store" function to a list of Store operands.
    ;; - This will have a return value that is not necessarily stored.
    ;; - (e.g. string concatentation for creating an html attribute.)
    ;; TODO REMOVE apply-func
    (define-syntax apply-func
      (syntax-rules ()
        ((apply-func FnStore Store1 StoreN ...)
          (create-op "nbdl.apply_func"
            (loc: (syntax-source-loc FnStore))
            (operands: FnStore Store1 StoreN ...)
            (attributes:)
            (result-types: (!nbdl.store))))))

    (define matching-results? #f)

    ;; Indicate that we require intermediate result values
    ;; from a ParamsSpec usually to become operands
    ;; to a call to visit. Fn will be called with mlir.values
    ;; or, in the case of expr+, a scheme procedure. (TODO)
    (define (%match-results ParamsSpec Fn)
      (define prev matching-results?)
      (dynamic-wind
        (lambda ()
          (set! matching-results? #t))
        (lambda ()
          (%match-params-spec ParamsSpec Fn))
        (lambda ()
          (set! matching-results? prev))))

    (define (%top-level Thunk)
      (define prev matching-results?)
      (dynamic-wind
        (lambda ()
          (set! matching-results? #f))
        Thunk
        (lambda ()
          (set! matching-results? prev))))

    (define %nbdl-discard '%nbdl-discard)

    (define (build-discard Loc Value)
      (create-op "nbdl.discard"
                   (loc: Loc)
                   (operands: Value)
                   (attributes:)
                   (result-types:))
      (list %nbdl-discard Loc))

    (define (discard-aux Loc Expr)
      (%match-results
        (list Expr)
        (lambda (Results)
          (define Value (car Results) )
          (build-discard Loc Value))))

    ;; Discard the result of an expression
    ;; (typically from `visit`.)
    (define-syntax discard
      (syntax-rules ()
        ((discard Value)
         (discard-aux (syntax-source-loc Value)
                      (%single-expr Value)))))

    (define (discarded? Obj)
      (and (pair? Obj) (eq? (car Obj) %nbdl-discard)))

    (define (discarded-loc Obj)
      (and (discarded? Obj) (cadr Obj)))

    (define (build-visit MatchingResults? Sfinae? Loc Results)
      (define SfinaeAttr
        (if Sfinae?
          (unit-attr)
          #f))
      (define ResultType
        (if MatchingResults?
          (!nbdl.store)
          !nbdl.unit))
      (define VisitResult
        (result
          (create-op "nbdl.visit"
                     (loc: Loc)
                     (operands: Results)
                     (attributes: ("sfinae" SfinaeAttr))
                     (result-types: ResultType))))
      (if MatchingResults?
        VisitResult
        (build-discard Loc VisitResult)))

    (define (visit-aux-aux MatchingResults? Sfinae? Loc ParamsSpec)
      (close-previous-scope)
      ;; This %expr is for the whole visit expr (ie its result).
      (if MatchingResults?
        (%expr Loc
          (lambda (Loc Fn)
            (%match-results
              ParamsSpec
              (lambda (Results)
                (Fn (build-visit MatchingResults? Sfinae? Loc Results))))))
        (%match-results ; Sfinae is #f
          ParamsSpec
          (lambda (Results)
            (build-visit MatchingResults? #f Loc Results)))))

    (define-syntax visit-aux
      (syntax-rules ()
        ((visit-aux Sfinae? Callee StoreN ...)
         (let ()
           (define MatchingResults? matching-results?)
           (define CalleeLoc (syntax-source-loc Callee))
           (define ParamsSpec
             (list
               (%single-expr+ Callee)
               (%single-expr StoreN) ...))
           (visit-aux-aux matching-results? Sfinae? CalleeLoc ParamsSpec)
           ))))

    ;; Analogous to std::visit but it takes stores
    ;; for all of its parameters including the callee.
    ;;
    ;; The callee accepts a member name which is mapped
    ;; to a member expression with the first argument as
    ;; the owning object.
    ;;
    ;; Return the result only if it is not discarded.
    (define-syntax visit
      (syntax-rules ()
        ((visit Callee StoreN ...)
         (visit-aux #f Callee StoreN ...))))

    ;; Create sfinae friendly visit when used as the direct input
    ;; to match-if conditional argument. The boundaries of the
    ;; sfinae check are limited to the call itself with all operands
    ;; being resolved.
    (define-syntax sfinae-visit
      (syntax-rules ()
        ((sfinae-visit Callee StoreN ...)
         (visit-aux #t Callee StoreN ...))))

    ;; Make a store callable (in scheme) strictly for use
    ;; with syntax that use => on procs or stores
    ;; representing a visitor.
    (define (make-visit-proc Store)
      (if (procedure? Store)
        Store
        (lambda (Arg)
          (visit Store Arg))))

    (define (match-each-aux Begin End Fn)
      (define ParamsSpec (list Begin End))
      (%match-results ParamsSpec
        (lambda (ParamVals)
          (%top-level
            (lambda ()
              (create-op "nbdl.match_each"
                         (loc: 0)
                         (operands: ParamVals)
                         (attributes:)
                         (result-types:)
                         (region: "body" ((Element : (!nbdl.store)))
                                  (Fn Element))))))))
                          ;; TODO support (visit Fn ...)

    ;; Match each element of a range. (side effects only)
    (define-syntax match-each
      (syntax-rules ()
        ((match-each Range Fn)
         (match-each (visit '.begin Range)
                     (visit '.end Range)
                     Fn))
        ((match-each Begin End Fn)
         (match-each-aux (%single-expr Begin)
                         (%single-expr End)
                         Fn))))

    (define-syntax match-aux
      (syntax-rules (=>)
        ((match-aux PathSpec
          (TypeN => FnN) ...)
         (%match-results (list PathSpec)
          (lambda (Store)
            (%top-level
              (lambda()
                (define (GetArgType T)
                  (if (eq? T "")
                    (!nbdl.store)
                    (!nbdl.store T)))
                (close-previous-scope)
                (create-op "nbdl.match"
                  (loc: (syntax-source-loc PathSpec))
                  (operands: Store)
                  (attributes:)
                  (result-types:)
                  (region: "overloads" ((OverloadArg : (GetArgType TypeN)))
                    ((make-visit-proc FnN) OverloadArg)) ...))))))))

    ;; Match a resolved object by its type.
    ;; - It is an error if a type appears more that once as an alternative.
    ;;   (Think type switch)
    ;; - Each clause should be
    ;;    (<cpp-typename> => proc) or
    ;;    (else => proc)
    ;;   where proc is a unary lambda receiving the matched store.
    ;; - All Types should not have cvref qualifiers.
    (define-syntax match
      (syntax-rules (else => store: key:)
        ((match PathSpec
          (else => DefaultFn))
         (match PathSpec
           ("" => DefaultFn)))
        ((match PathSpec
          (Type1 => Fn1)
          (TypeN => FnN) ...
          (else => DefaultFn))
         (match PathSpec
           (Type1 => Fn1)
           (TypeN => FnN) ...
           ("" => DefaultFn)))
        ((match PathSpec
           (Type1 => Fn1)
           (TypeN => FnN) ...)
         (match-aux (%single-expr PathSpec)
           (Type1 => Fn1)
           (TypeN => FnN) ...))
        ))

    ; Visit store and do nothing even if there is
    ; butterscotch in a crystal bowl on the table.
    (define (noop Store)
      (create-op "nbdl.noop"
        (loc: 0)
        (operands: Store)
        (attributes:)
        (result-types:))
      (when #f #f))

    (define (build-match-if Loc CondResult ThenThunk ElseThunk)
      (define ThenArgT
        (get-type CondResult))
      (create-op "nbdl.match_if"
                 (loc: Loc)
                 (operands: CondResult)
                 (attributes:)
                 (result-types:)
                 (region: "then" ((ThenArg : ThenArgT))
                          (%top-level
                            (lambda () (ThenThunk ThenArg))))
                 (region: "else" () (%top-level ElseThunk))))

    (define (match-if-aux Loc CondExprFn ThenThunk ElseThunk)
      (define CondExpr
        (%expr Loc CondExprFn))
      (define ParamsSpec
        (list CondExpr))
      (%match-results
        ParamsSpec
        (lambda (Results)
          (define CondResult (car Results))
          (build-match-if Loc CondResult ThenThunk ElseThunk)))
      (if #f #f)) ; return undefined

    ; The syntax match-if is not so different from
    ; if except that it operates on expressions that
    ; resolve stores (ie via get, visit, et al.)
    ; If Else is not specified then yield 'false.
    (define-syntax match-if
      (syntax-rules ()
        ((match-if Cond Then)
         (match-if Cond Then (discard 'false)))
        ((match-if Cond Then Else)
         (match-if-aux (syntax-source-loc Cond)
                        (lambda (Loc Fn)
                          (%match-expr Loc Cond Fn))
                        (lambda (ThenArg) Then)
                        (lambda () Else)))))

    ; This is basically a copy of R7RS `cond` syntax
    ; adapted to use match-if.
    (define-syntax match-cond
      (syntax-rules (else =>)
        ((match-cond (else result1 result2 ...))
         (begin result1 result2 ...))
        ((match-cond (test => result))
         (match-if-aux (syntax-source-loc test)
                       (lambda (Loc Fn) (%match-expr Loc test Fn))
                       (lambda (ThenArg) ((make-visit-proc result) ThenArg))
                       (lambda () 0)))
        ((match-cond (test => result) clause1 clause2 ...)
         (match-if-aux (syntax-source-loc test)
                       (lambda (Loc Fn) (%match-expr Loc test Fn))
                       (lambda (ThenArg) ((make-visit-proc result) ThenArg))
                       (lambda () (match-cond clause1 clause2 ...))))
        ((match-cond (test)) test)
        ((match-cond (test) clause1 clause2 ...)
         (match-cond (test => (lambda (DiscardMe) 0))
                     clause1 clause2 ...))
        ((match-cond (test result1 result2 ...))
         (match-if test (begin result1 result2 ...)))
        ((match-cond (test result1 result2 ...)
               clause1 clause2 ...)
         (match-if test
           (begin result1 result2 ...)
           (match-cond clause1 clause2 ...)))))

    (define (write-cpp Name)
      (define Op
        (cond
          ((named-fn? Name)
            (let ()
              (define-values (_ _ FuncOp)
                (apply values Name))
              FuncOp))
         (else (module-lookup main-module Name))))
      (translate-cpp Op)
      (flush-tokens)
      (newline))

    (define (dump-op name)
      (define Op
        (module-lookup main-module name))
      (dump Op)
      (newline))

    (define (dump-nbdl-module)
      (dump main-module))

    (define (write-nbdl-module)
      (write main-module)
      (newline))

    (define export-cpp-names '())

    (define-syntax export-cpp
      (syntax-rules ()
        ((export-cpp Name ...)
         (set! export-cpp-names
           (append
             (list (namespace-prefix 'Name) ...)
             export-cpp-names)))))

    ; FIXME Remove once we call flatten pass on every top-level-op.
    (define (run-pass-nbdl-flatten)
      (nbdl_run_flatten_pass
        main-module current-schir-clang))

  ) ; end of... begin
  (export
    define-context
    define-store
    store-compose
    variant
    store
    get
    match
    match-cond
    match-each
    match-if
    match-params-fn
    visit
    sfinae-visit
    noop
    export-cpp

    ;; Reexport some base stuff
    define
    define-syntax
    error
    syntax-rules
    if
    lambda
    set!
    quote
    quasiquote
    source-loc
    type
    dump

    ;; Stuff that should be broken out as a common details lib
    top-level-op
    make-named-fn
    write-cpp
    dump-op
    dump-nbdl-module
    write-nbdl-module
    run-pass-nbdl-flatten
    )
)  ; end of (nbdl spec)
