(import (schir base))

(define-library (nbdl spec)
  (import (schir base)
          (schir mlir)
          (schir mlir all-passes)
          (schir llvm pass)
          (schir clang))
  (begin
    ;; Note that the %match functions in this implementation
    ;; use a CPS style where callbacks can be called multiple
    ;; times to generate code for the regions of every combination
    ;; of possible branches.

    (load-plugin "libNbdl.so")
    (define translate-cpp
      (load-builtin "nbdl_spec_translate_cpp"))
    (define declare-cpp
      (load-builtin "nbdl_spec_declare_cpp"))
    (define define-lowered-wrapper
      (load-builtin "nbdl_spec_define_lowered_wrapper"))
    (define mark-c-adapter
      (load-builtin "nbdl_spec_mark_c_adapter"))
    (define close-previous-scope
      (load-builtin "nbdl_spec_close_previous_scope"))
    (define register-nbdl-dialect
      (load-builtin "nbdl_spec_register_nbdl_dialect"))
    (define store-value?
      (load-builtin "nbdl_spec_is_store"))
    (define nbdl_spec_create_store_type
      (load-builtin "nbdl_spec_create_store_type"))
    (define nbdl_run_flatten_pass
      (load-builtin "nbdl_run_flatten_pass"))
    (define nbdl_run_inference_passes
      (load-builtin "nbdl_run_inference_passes"))
    (define nbdl_canonicalize_cpp_types
      (load-builtin "nbdl_canonicalize_cpp_types"))
    (define define-store-op?
      (load-builtin "nbdl_spec_is_define_store"))
    (define nbdl_spec_type_to_cpp_type
      (load-builtin "nbdl_spec_type_to_cpp_type"))
    (define nbdl_spec_cpp_alias_type
      (load-builtin "nbdl_spec_cpp_alias_type"))

    ;; Create a !nbdl.cpp_alias type with a C++ typename as written.
    ;; The flatten pass replaces it with a !nbdl.cpp type
    ;; with the canonical typename.
    (define (!cpp Typename)
      (nbdl_spec_cpp_alias_type Typename))

    ;; Lift a string-like or named-store to a C++ type
    ;; or return the mlir type as is.
    (define (%cpp-or-type T)
      (cond
        ((or (string? T) (symbol? T)) (!cpp T))
        ((named-store? T) (named-store->cpp T))
        (else T)))

    ;; Create a !nbdl.store type where string-likes are lifted to C++ types.
    (define (!nbdl.store . Alts)
      (apply nbdl_spec_create_store_type
             (map %cpp-or-type Alts)))

    ;; "Cpp" module will translate to c++ via translate-cpp.
    (define main-module (create-top-module "nbdl_spec_module_cpp"))

    (register-nbdl-dialect)
    (load-dialect "func")
    (load-dialect "schir")
    (load-dialect "nbdl")
    (load-dialect "arith")

    ;; Create the symbol name for a top level op.
    ;; Exported names are qualified with the current C++ namespace
    ;; so they are distinct from the names exported by other modules.
    ;; TODO We assume a single translation unit. To support multiple
    ;;      translation units, we need to properly handle the external
    ;;      linkage of lowered functions and anonymous namespaces.
    (define (top-level-name Name)
      (if (or (memq Name export-cpp-names)
              (memq Name export-c-names)
              (memq Name export-c-internal-names))
        (namespace-prefix Name)
        Name))

    (define llvm-module #f)

    ;; Copy a FuncOp into Module to be lowered.
    (define (copy-to-lowered-module Module FuncOp)
      (with-module-builder
        Module
        (lambda ()
          (define Copy (copy-op FuncOp))
          ;; Mark to have an abi adapter created to allow calling from C++.
          (when (%export-op? export-c-ops FuncOp)
            (mark-c-adapter Copy))
          Copy)))

    ;; Copy a FuncOp into the llvm-module.
    (define (lower-to-llvm FuncOp)
      (unless llvm-module
        (set! llvm-module (create-top-module "nbdl_spec_llvm_module")))
      (copy-to-lowered-module llvm-module FuncOp))

    ;; An element (Name Op) of an exported ops list.
    (define (%make-export Name Op)
      (list Name Op))

    (define (%export-op Export)
      (cadr Export))

    ;; Return #t if Op is an element of ExportOps.
    (define (%export-op? ExportOps Op)
      (let loop ((Rest ExportOps))
        (cond
          ((null? Rest) #f)
          ((eq? Op (%export-op (car Rest))) #t)
          (else (loop (cdr Rest))))))

    ;; Add the element (Name Op) to the end of ExportOps so
    ;; elements are in order of definition.
    (define (%add-export-op Loc ExportOps Name Op)
      (when (assq Name ExportOps)
        (error-with-loc Loc "exported name defined more than once: {}" Name))
      (append ExportOps (list (%make-export Name Op))))

    ;; Call Fn with each Op in ExportOps (in order of definition)
    ;; that satisfies Pred.
    (define (%finalize-exports ExportOps Pred Fn)
      (let loop ((Rest ExportOps))
        (unless (null? Rest)
          (let ((Op (%export-op (car Rest))))
            (when (Pred Op)
              (Fn Op)))
          (loop (cdr Rest)))))

    ;; Functions exported via export-c or export-c-internal that are
    ;; lowered to LLVM by finalize-module (ie by define-fn.)
    (define %lower-to-llvm-ops '())

    ;; Return a procedure that emits C++ for an Op via Translate.
    (define (%emit-cpp Translate)
      (lambda (Op)
        (with-module-builder
          main-module
          (lambda ()
            (Translate Op lexer-writer)
            (flush-tokens)))))

    (define (%function-op? Op)
      (not (define-store-op? Op)))

    ;; Finish the module by writing c++ translations, forward declarations,
    ;; wrappers of lowered functions, and lowering and injecting
    ;; backend modules.
    (define (finalize-module)
      (%finalize-exports export-cpp-ops define-store-op?
                         (%emit-cpp translate-cpp))
      ;; Declarations are written with canonical C++ types
      ;; to match the definitions.
      (nbdl_canonicalize_cpp_types main-module current-schir-clang)
      ;; Infer types and simplify operations.
      (run-inference-passes)
      (%finalize-exports export-cpp-ops %function-op?
                         (%emit-cpp declare-cpp))
      ;; Lowered functions are called from C++ via wrappers.
      (%finalize-exports export-c-ops %function-op?
                         (%emit-cpp define-lowered-wrapper))
      (%finalize-exports export-cpp-ops %function-op?
                         (%emit-cpp translate-cpp))
      (%finalize-exports export-c-ops
                         (lambda (Op) (memq Op %lower-to-llvm-ops))
                         lower-to-llvm)
      (%finalize-exports export-c-internal-ops
                         (lambda (Op) (memq Op %lower-to-llvm-ops))
                         lower-to-llvm)
      (when llvm-module
        (apply run-passes llvm-module "nbdl-lower"
               (append %lowering-passes
                       (list "nbdl-to-llvm" "nbdl-c-adapter")))
        (inject-module llvm-module)
        (set! llvm-module #f)))

    ;; Thunk receives the symbol name and should return
    ;; a new top level operation using the current module builder.
    ;; Initial passes and C++ translation are deferred to finalize-module.
    (define (top-level-op Loc Name Thunk)
      (when (and (memq Name export-cpp-names)
                 (memq Name export-c-names))
        (error-with-loc Loc "name cannot be exported to both C++ and C: {}"
                        Name))
      (when (and (memq Name export-c-internal-names)
                 (or (memq Name export-cpp-names)
                     (memq Name export-c-names)))
        (error-with-loc Loc "internal name cannot also be exported: {}"
                        Name))
      (with-module-builder
        main-module
        (lambda ()
          (define TopLevelOp (Thunk (top-level-name Name)))
          ;; The verify pass may also raise a more specific error.
          (verify TopLevelOp)
          (cond
            ((memq Name export-cpp-names)
             (set! export-cpp-ops
               (%add-export-op Loc export-cpp-ops Name TopLevelOp)))
            ((memq Name export-c-names)
             (set! export-c-ops
               (%add-export-op Loc export-c-ops Name TopLevelOp)))
            ((memq Name export-c-internal-names)
             (set! export-c-internal-ops
               (%add-export-op Loc export-c-internal-ops Name TopLevelOp))))
          TopLevelOp)))

    (define !nbdl.member_name (type "!nbdl.member_name"))
    (define !nbdl.func_name (type "!nbdl.func_name"))
    (define !nbdl.unit (type "!nbdl.unit"))
    (define !nbdl.unknown (type "!nbdl.unknown"))
    (define i32 (type "i32"))
    (define f32 (type "f32"))

    ;; Not used types
    ; (define !nbdl.tag (type "!nbdl.tag")) ; Not used.
    ; (define !nbdl.empty (type "!nbdl.empty"))

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
        ((exact-integer? Arg)
          (build-literal Loc (attr (number->string Arg) i32) i32))
        ((flonum? Arg)
          (build-literal Loc (attr (number->string Arg) f32) f32))
        ((string? Arg)
          (build-literal Loc (string-attr Arg)
                         (!cpp 'std::string_view)))
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
        ;; Procedures pass through (via expr+).
        ((procedure? Expr)
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

    (define (build-literal Loc Arg T)
      (result
        (create-op "nbdl.literal"
          (loc: Loc)
          (operands:)
          (attributes: ("value" Arg))
          (result-types: T))))

    (define (build-constexpr Loc ExprStr)
      (when (member-name-literal? ExprStr)
        (error "unexpected member name: {}" ExprStr))
      (result
        (create-op "nbdl.constexpr"
          (loc: Loc)
          (operands:)
          (attributes: ("expr" (string-attr ExprStr)))
          (result-types: !nbdl.unknown))))

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
      (define Name
        (if (named-store? Typename)
          (named-store-sym-name Typename)
          Typename))
      (result
        (create-op
          "nbdl.store"
          (loc: Loc)
          (operands: InitArgs)
          ; TODO Mangle Typename
          (attributes: ("name" (flat-symbolref-attr Name)))
          (result-types: (!nbdl.store Typename))
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
         (define Name
           (let ((SymName #f))
            (let ((DefineStoreOp
                   (top-level-op
                     (syntax-source-loc Name)
                     'Name
                     (lambda (SymName_)
                       (define Loc (syntax-source-loc Name))
                       (set! SymName SymName_)
                       (create-op
                         "nbdl.define_store"
                         (loc: Loc)
                         (operands:)
                         (attributes: ("sym_name" (string-attr SymName)))
                         (result-types:)
                         (region: "body" ((InitParams : (!nbdl.store)) ...)
                                  (define-store-aux
                                    Loc
                                    (lambda (ProcessBody)
                                      ;; Ensure nonempty lambda.
                                      (ProcessBody StoreFunctionalN) ... #t)
                                    )))))))
             (make-named-store SymName DefineStoreOp)))))))

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

    (define %named-store '%named-store)

    ;; Denote the name of a store defined as top level operation
    ;; in the module.
    (define (make-named-store SymbolName DefineStoreOp)
      (list %named-store SymbolName DefineStoreOp))

    (define (named-store? Value)
      (and (pair? Value)
           (eq? %named-store (car Value))))

    (define (named-store-sym-name NamedStore)
      (cadr NamedStore))

    ;; Convert a named-store to a c++ type.
    (define (named-store->cpp NamedStore)
      (!cpp (named-store-sym-name NamedStore)))

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
          (when (and (value? RootStore)
                     (not (store-value? RootStore)))
            (error-with-loc (source-loc PathNodes)
                            "expecting a store as the root of a path (see lift-store)"))
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

    ;; Match a store with unit-key.
    (define (%match-unit Loc Store Fn)
      (%match-key Loc Store '() Fn))

    ;; We have mlir.values for both Store and Key
    (define (%match-key Loc Store Key Fn)
      (create-op "nbdl.match"
        (loc: Loc)
        (operands: Store Key)
        (attributes:)
        (result-types:)
        (region: "overloads" ((ResolvedStore : (!nbdl.store)))
          (Fn ResolvedStore))))

    (define (member-name-literal? PathNode)
      (and (symbol? PathNode)
           (eq? (string-ref PathNode 0) #\.)))

    (define (%match-path-node Store Loc PathNode Fn)
      (close-previous-scope)
      (let ((PathNode
              (maybe-build-expr+ Loc PathNode)))
        (cond
          ; TODO A define-match-fn should lift a proc to take Fn.
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
      (result
        (create-op "nbdl.get"
          (loc: Loc)
          (operands: Store KeyVal)
          (attributes:)
          (result-types: (!nbdl.store)))))

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
      ;; The result is not a store.
      (define ResultType
        (if MatchingResults?
          !nbdl.unknown
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
                (define Callee (car Results))
                (if (procedure? Callee)
                  ;; The result may be a value, a literal,
                  ;; or another expr.
                  (%match-expr Loc (apply Callee (cdr Results)) Fn)
                  (Fn (build-visit MatchingResults? Sfinae? Loc Results)))))))
        (begin
          (%match-results ; Sfinae is #f
            ParamsSpec
            (lambda (Results)
              (define Callee (car Results))
              (if (procedure? Callee)
                ;; The procedure is applied for its side effects
                ;; (e.g. a visit in its body is discarded.)
                (%top-level
                  (lambda () (apply Callee (cdr Results))))
                (build-visit MatchingResults? #f Loc Results))))
          (list %nbdl-discard Loc))))

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

    (define (match-each-aux Loc RangeExpr Fn)
      (close-previous-scope)
      (%match-results (list RangeExpr)
        (lambda (Results)
          (define Range (car Results))
          (unless (store-value? Range)
            (error-with-loc Loc
                            "expecting a store to match-each (see lift-store)"))
          (%top-level
            (lambda ()
              (create-op "nbdl.match_each"
                         (loc: Loc)
                         (operands: Range)
                         (attributes:)
                         (result-types:)
                         (region: "body" ((Element : (!nbdl.store)))
                                  ((make-visit-proc Fn) Element))))))))

    ;; Match each element of a store wrapping a range-like object
    ;; (e.g. a C++ range or a memref.) (side effects only)
    ;; Fn is a unary procedure or a store to visit with each element.
    (define-syntax match-each
      (syntax-rules ()
        ((match-each Range Fn)
         (match-each-aux (syntax-source-loc Range)
                         (%single-expr Range)
                         Fn))))

    (define-syntax match-aux
      (syntax-rules (=>)
        ((match-aux PathSpec
          (TypeN => FnN) ...)
         (let ((PathSpecExpr PathSpec))
          (%match-results (list PathSpecExpr)
          (lambda (Store)
            (unless (store-value? (car Store))
              ;; Use the source location of the %expr.
              (error-with-loc (cadr PathSpecExpr)
                              "expecting a store to match (see lift-store)"))
            (%top-level
              (lambda()
                ; Canonicalize C++ typenames so types can be compared.
                (define (GetArgType T)
                  (cond
                    ((eq? T "") (!nbdl.store))
                    ((or (symbol? T) (string? T))
                     (!nbdl.store T))
                    (else (!nbdl.store T))))
                (close-previous-scope)
                (create-op "nbdl.match"
                  (loc: (syntax-source-loc PathSpec))
                  (operands: Store)
                  (attributes:)
                  (result-types:)
                  (region: "overloads" ((OverloadArg : (GetArgType TypeN)))
                    ((make-visit-proc FnN) OverloadArg)) ...)))))))))

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

    ;; Define a generic function to receive a matched set of parameters.
    ;; All parameters are unresolved stores which may be matched within
    ;; the body (e.g. via match-params.)
    (define-syntax define-match-fn
      (syntax-rules ()
        ((define-match-fn Name (Arg ...) Body ...)
         (define Name
           (let ((SymName #f))
            (let ((FuncOp (top-level-op
                           (syntax-source-loc Name)
                           'Name
                           (lambda (SymName_)
                             (set! SymName SymName_)
                             (create-op
                               "func.func"
                               (loc: (syntax-source-loc Name))
                               (operands:)
                               (attributes:
                                 ("sym_name" (string-attr SymName))
                                 ("function_type"
                                  (type-attr
                                    (%function-type
                                      (make-vector
                                        (length '(Arg ...))
                                        (!nbdl.store))
                                      #()))))
                               (result-types:)
                               (region: "body" ((Arg : (!nbdl.store)) ...)
                                        Body ...))))))
             (make-named-fn SymName FuncOp)))))))

    ;; Define a normal function whose parameter and result types are
    ;; mlir types where string-likes are lifted to C++ types.
    ;; Use lift-store on a parameter to use it with Nbdl operations.
    ;; A result type of !nbdl.unknown is inferred from the returned values
    ;; by the inference passes. (see register-inference-pass)
    (define-syntax define-fn
      (syntax-rules (: ->)
        ((define-fn Name ((Arg : ArgT) ...) -> (RetT ...) Body1 BodyN ...)
         (define Name
           (let ((SymName #f))
            (let ((FuncOp
                   (top-level-op
                     (syntax-source-loc Name)
                     'Name
                     (lambda (SymName_)
                       (set! SymName SymName_)
                       (create-op
                         "func.func"
                         (loc: (syntax-source-loc Name))
                         (operands:)
                         (attributes:
                           ("sym_name" (string-attr SymName))
                           ("function_type"
                            (type-attr
                              (%function-type
                                (vector (%cpp-or-type ArgT) ...)
                                (vector (%cpp-or-type RetT) ...)))))
                         (result-types:)
                         (region: "body" ((Arg : (%cpp-or-type ArgT))
                                          ...)
                                  Body1 BodyN ...))))))
             ;; Process exports if any.
             (when (or (memq 'Name export-c-names)
                       (memq 'Name export-c-internal-names))
               (set! %lower-to-llvm-ops
                 (cons FuncOp %lower-to-llvm-ops)))
             (make-named-fn SymName FuncOp)))))))

    (define (build-lift-store Loc Arg)
      (define Value (maybe-build-expr Loc Arg))
      (define ResultT
        (cond
          ((not (value? Value))
           (error-with-loc Loc "expecting mlir value to lift: {}" Value))
          ((store-value? Value)
           (error-with-loc Loc "value is already a store"))
          (else (!nbdl.store (get-type Value)))))
      (result
        (create-op "nbdl.lift_store"
                   (loc: Loc)
                   (operands: Value)
                   (attributes:)
                   (result-types: ResultT))))

    ;; An expr (e.g. visit) is lifted when it is resolved.
    (define (lift-store-aux Loc Arg)
      (if (expr? Arg)
        (%expr Loc
               (lambda (Loc Fn)
                 (%invoke-expr Arg
                   (lambda (Value)
                     (Fn (build-lift-store Loc Value))))))
        (build-lift-store Loc Arg)))

    (define-syntax lift-store
      (syntax-rules ()
        ((lift-store Value)
         (lift-store-aux (syntax-source-loc Value) Value))))

    (define (return-aux Loc Exprs)
      (close-previous-scope)
      (%match-results
        Exprs
        (lambda (Results)
          (create-op "nbdl.return"
                     (loc: Loc)
                     (operands: Results)
                     (attributes:)
                     (result-types:))))
      (if #f #f)) ; return undefined

    ;; Return the result (if any) from a function defined with define-fn.
    (define-syntax return
      (syntax-rules ()
        ((return)
         (return-aux (current-source-loc) '()))
        ((return Expr)
         (return-aux (syntax-source-loc Expr)
                     (list (%single-expr Expr))))))

    ;; Match stores via a let* like syntax.
    ;; Optionally, append a type constraint via `:` indentifier.
    ;; For example,
    ;;   (match-params ((V1 Expr1)
    ;;                  (V2 : Type Expr2))
    ;;     Body ...)
    (define-syntax match-params
      (syntax-rules (:)
        ((match-params () Body ...)
         (begin Body ...))
        ((match-params ((V : T Expr) Rest ...) Body ...)
         (match Expr
           (T => (lambda (V) (match-params (Rest ...) Body ...)))))
        ((match-params ((V Expr) Rest ...) Body ...)
         (match Expr
           (else => (lambda (V) (match-params (Rest ...) Body ...)))))))

    ;; Write operations translated c++ to stdout.
    (define (write-cpp Name)
      (define Op
        (cond
          ((named-fn? Name)
            (let ()
              (define-values (_ _ FuncOp)
                (apply values Name))
              FuncOp))
          ((named-store? Name)
            (let ()
              (define-values (_ _ DefineStoreOp)
                (apply values Name))
              DefineStoreOp))
         (else (module-lookup main-module Name))))
      (translate-cpp Op)
      (newline))

    ;; Map a mlir.type to a !nbdl.cpp type with a canonical
    ;; C++ typename or #f if the type is not mappable to C++.
    (define (type->cpp T)
      (nbdl_spec_type_to_cpp_type T current-schir-clang))

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

    ;; Track the names of exports.
    (define export-cpp-names '())
    (define export-c-names '())
    (define export-c-internal-names '())

    ;; Track exported ops as (Name Op) in order of definition.
    (define export-cpp-ops '())
    (define export-c-ops '())
    (define export-c-internal-ops '())

    (define-syntax export-cpp
      (syntax-rules ()
        ((export-cpp Name ...)
         (set! export-cpp-names
           (append
             export-cpp-names
             (list 'Name ...))))))

    (define-syntax export-c
      (syntax-rules ()
        ((export-c Name ...)
         (set! export-c-names
           (append
             export-c-names
             (list 'Name ...))))))

    ;; Export to the LLVM module without exposing declarations to C++.
    (define-syntax export-c-internal
      (syntax-rules ()
        ((export-c-internal Name ...)
         (set! export-c-internal-names
           (append
             export-c-internal-names
             (list 'Name ...))))))

    (define (run-pass-nbdl-flatten)
      (nbdl_run_flatten_pass
        main-module current-schir-clang))

    ;; Pass pipeline strings for the inference stage
    ;; (in order of registration.)
    (define %inference-passes '())

    ;; Register a pass pipeline (string) to be run with the flatten pass
    ;; until a fixed point is reached. This allows the types of values
    ;; from other dialects to be inferred by their own passes.
    (define (register-inference-pass Pipeline)
      (unless (string? Pipeline)
        (error "expecting pass pipeline string: {}" Pipeline))
      (unless (member Pipeline %inference-passes)
        (set! %inference-passes
          (append %inference-passes (list Pipeline)))))

    ;; Pass pipeline strings for the lowering stage.
    ;; (in order of registration.)
    (define %lowering-passes '())

    ;; Register a pass pipeline (string) to lower the operations of other
    ;; dialects to dialects that nbdl-to-llvm can lower to LLVM or SPIRV.
    (define (register-lowering-pass Pipeline)
      (unless (string? Pipeline)
        (error "expecting pass pipeline string: {}" Pipeline))
      (unless (member Pipeline %lowering-passes)
        (set! %lowering-passes
          (append %lowering-passes (list Pipeline)))))

    ;; Run the flatten pass and the registered inference passes.
    ;; It is an error if the result type of any function is not inferred.
    (define (run-inference-passes)
      (apply nbdl_run_inference_passes
             main-module current-schir-clang %inference-passes))

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
    define-match-fn
    define-fn
    lift-store
    return
    register-inference-pass
    register-lowering-pass
    match-params
    visit
    sfinae-visit
    noop
    export-cpp
    export-c
    export-c-internal
    finalize-module

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
    type->cpp
    !cpp
    !nbdl.unknown
    dump

    ;; Stuff that should be broken out as a common details lib
    top-level-op
    make-named-fn
    write-cpp
    dump-op
    dump-nbdl-module
    write-nbdl-module
    run-pass-nbdl-flatten
    run-inference-passes
    )
)  ; end of (nbdl spec)
