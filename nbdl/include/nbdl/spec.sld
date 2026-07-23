(import (schir base))

(define-library (nbdl spec)
  (import (schir base)
          (schir mlir)
          (schir clang))
  (begin
    ;; Note that the %match functions in this implementation
    ;; use a CPS style where callbacks can be called multiple
    ;; times to generate code for the regions of every combination
    ;; of possible branches.

    (load-plugin "libNbdl.so")
    (define %build-match-params
      (load-builtin "nbdl_spec_build_match_params"))
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

    ;; "Cpp" module will translate to c++ via translate-cpp.
    (define module-cpp (create-top-module "nbdl_spec_module_cpp"))

    (register-nbdl-dialect)
    (load-dialect "func")
    (load-dialect "schir")
    (load-dialect "nbdl")

    ;; Thunk should return a new top level operation using the provided
    ;; module builder. The new operation is immediately translated to
    ;; that also translates to C++.
    ;; This is done immediately to make the C++ type available for
    ;; introspection when making subsequent operations. This is due
    ;; to the way we allow interleaving C++ and Scheme, but in practice
    ;; it might not be necessary to support this.
    (define (top-level-cpp-op Thunk)
      (with-module-builder
        module-cpp
        (lambda ()
          (define TopLevelOp (Thunk))
          ; The verify pass may also raise a more specific error.
          (unless (verify TopLevelOp)
            (error "operation failed verification: {}" TopLevelOp))
          (translate-cpp TopLevelOp lexer-writer)
          (flush-tokens)
          TopLevelOp)))

    ; FIXME change to "!nbdl.variant" once its in the compiler.
    (define !nbdl.variant (type "!nbdl.store")) ;"!nbdl.variant"))
    (define !nbdl.member_name (type "!nbdl.member_name"))
    (define !nbdl.unit (type "!nbdl.unit"))
    (define i32 (type "i32"))

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

    ;; Create a thunk that should receive a location and callback
    ;; to resolve a value once its dependencies are resolved.
    ;; (e.g. arguments to visit)
    (define-syntax %expr
      (syntax-rules ()
        ((%expr Loc Thunk)
         (list '%nbdl-expr Loc Thunk))))

    (define (expr? Arg)
      (and (pair? Arg) (eqv? '%nbdl-expr (car Arg))))

    ; Maybe lift to a LiteralOp or ConstexprOp.
    (define (maybe-build-expr Loc Arg)
      (cond
        ((member-name-expr? Arg)
          (error "unexpected member name literal"))
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
      (if (member-name-expr? Arg)
        (build-member-name Loc Arg)
        (maybe-build-expr Loc Arg)))

    (define (%match-expr-impl Expr Fn)
      (cond
        ((value? Expr)
         (Fn Expr))
        ((procedure? Expr)
         (Expr Fn))
        ((expr? Expr) ;; FIXME this go here?
         (let ((Loc (cadr Expr))
               (Thunk (car (cddr Expr))))
           (Thunk Loc Fn)))
        ((path? Expr)
         (%match-path-spec Expr Fn))
        (else (error "unable to resolve value: {}" Expr)))
      ; Return something... unspecified.
      (if #f #f))

    (define (%match-expr Loc ExprArg Fn)
      (define Expr (maybe-build-expr Loc ExprArg))
      (%match-expr-impl Expr Fn))

    (define (%match-expr+ Loc ExprArg Fn)
      (define Expr (maybe-build-expr+ Loc ExprArg))
      (%match-expr-impl Expr Fn))

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
      (when (member-name-expr? ExprStr)
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
        ((member-name-expr? Key)
           (build-member-name Loc Key))
        (else (build-constexpr Loc Key))))

    ;; Expects name begins with a .
    (define (build-member-name Loc Name)
      (define StrippedName
        (begin
          (unless (member-name-expr? Name)
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

    ; FIXME This inserts a mlir.operation which means it has to
    ;       be invoked in the right context to insert properly.
    ;       Consider making a StoreSpec or something to prevent
    ;       this potentially surprising requirement.
    (define-syntax store
      (syntax-rules (init-args:)
        ((store Typename)
         (store Typename (init-args:)))
        ((store Typename (init-args: InitArgs ...))
         (result
           (create-op "nbdl.store"
             (loc: (syntax-source-loc Typename))
             (operands: (map maybe-build-expr
                             '((syntax-source-loc InitArgs) ...)
                             (list InitArgs ...)))
             (attributes: ("name" (flat-symbolref-attr Typename)))
             (result-types: (!nbdl.store))
             )))))

    (define-syntax store-compose
      (syntax-rules ()
        ((store-compose Key Store)
         (lambda (ParentStore)
           (let ((KeyLoc (syntax-source-loc Key)))
             (result
               (create-op "nbdl.store_compose"
                 (loc: KeyLoc)
                 (operands: (build-store-key KeyLoc Key) Store ParentStore)
                 (attributes:)
                 (result-types: (!nbdl.store))
                 )))))))

    ; FIXME Use a StoreSpec instead if inserting directly. (See `store`.)
    (define-syntax variant
      (syntax-rules ()
        ((variant Store1 StoreN ...)
          (result
            (create-op "nbdl.variant"
              (loc: (syntax-source-loc Store1))
              (operands: Store1 StoreN ...)
              (attributes:)
              (result-types: !nbdl.variant)
              )))))

    (define (define-store-aux Loc BodyThunk)
      (define Parent (build-unit))
      (define (ProcessBody BodyEl)
        (set! Parent
          (cond
            ; StoreFunctional
            ((procedure? BodyEl)
             (BodyEl Parent))
            ; Store
            ((value? !nbdl.unit Parent)
             BodyEl)
            (else
              (error "expecting store: {}" BodyEl)))))
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
           (define Name 'Name)
           (top-level-cpp-op
             (lambda ()
               (define Loc (syntax-source-loc Name))
               (create-op
                 "nbdl.define_store"
                 (loc: Loc)
                 (operands:)
                 (attributes: ("sym_name" (string-attr Name)))
                 (result-types:)
                 (region: "body" ((InitParams : (!nbdl.store)) ...)
                          (define-store-aux
                            Loc
                            (lambda (ProcessBody)
                              ;; Ensure nonempty lambda.
                              (ProcessBody StoreFunctionalN) ... #t)
                          )))))))))

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

    ; Provide the callback function for match-params-fn syntax
    ; %FnVal is a mlir.value.
    (define (%match-params-resolver Loc %FnVal)
      (lambda ParamsSpec_
        (define (ToExpr Param)
          (%expr Loc
                 (lambda (Loc Fn)
                   (%match-expr Loc Param Fn))))
        (define ParamsSpec
          (map ToExpr ParamsSpec_))
        (define (VisitFn ParamVals)
          (build-resolve-params Loc %FnVal ParamVals))
        (close-previous-scope)
        (%match-params-spec ParamsSpec VisitFn)))

    ;; Define a function to receive a matched set of parameters.
    ;; Each path node should be of the format:
    ;;  (%Kind Loc Args...)
    ;; or specifically for nested pathspecs:
    ;;  (%nbdl-path PathNodes...)
    (define-syntax match-params-fn
      (syntax-rules ()
        ((match-params-fn name (stores ... fn) body ...)
         (begin
           (define name 'name)
           (top-level-cpp-op
             (lambda ()
               (%build-match-params
                 'name
                 (length '(stores ...))
                 (lambda (stores ... %FnVal)
                   (define Loc (source-loc name))
                   (define Fn
                     (%match-params-resolver Loc %FnVal))
                   ((lambda (fn) body ...) Fn)
                   ))))
           ))))

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
      (%map-params %match-path-spec ParamsSpec ParamsFn))

    (define (%match-path-spec PathSpec Fn)
      (cond
        ((value? PathSpec)
         (Fn PathSpec))
        ((expr? PathSpec)
         (let ((Loc (cadr PathSpec))
               (Thunk (car (cddr PathSpec))))
           (Thunk Loc Fn)))
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
      (define (ReflectAlts StoreAlt)
        (lambda (KeyAlt)
          (reflect-match Loc StoreAlt KeyAlt)))
      (define StoreAlts (get-store-alts Store))
      (define KeyAlts (get-store-alts Key))
      (define MatchedAlts
        (if (and StoreAlts KeyAlts)
          (apply append (map apply (map ReflectAlts StoreAlts) KeyAlts))
          '()))
      (define StoreT
        (apply !nbdl.store MatchedAlts))
      (create-op "nbdl.match"
        (loc: Loc)
        (operands: Store Key)
        (attributes:)
        (result-types:)
        (region: "overloads" ((ResolvedStore : StoreT))
          (Fn ResolvedStore))))

    (define (member-name-expr? PathNode)
      (and (symbol? PathNode)
           (eq? (string-ref PathNode 0) #\.)))

    (define (%match-path-node Store Loc PathNode Fn)
      (close-previous-scope)
      (let ((PathNode
              (maybe-build-expr+ Loc PathNode)))
        (cond
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
      ; TODO Use get-single-alternative
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
        (error "expecting list of params: {}" ParamVals))
      (let ()
        (define Result
          (result
            (create-op "nbdl.visit"
                       (loc: Loc)
                       (operands: FnVal ParamVals)
                       (attributes:)
                       (result-types: !nbdl.unit))))
        (create-op "nbdl.discard"
                   (loc: Loc)
                   (operands: Result)
                   (attributes:)
                   (result-types:))))



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
          ((else (error "invalid path object: {}" path)))
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
    ;; to a call to visit.
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

    ;; Map a list of mlir store values
    ;; to strings of C++ exprs type inference.
    ;; Return #f if any of the values do no map to a single alternative.
    (define (get-infer-args Args)
      (define (DeclVal T)
        (string-append "::nbdl::detail::declval<" T ">()"))
      (define StoreAlts
        (map get-single-alternative Args))
      (if (every symbol? StoreAlts)
        (map DeclVal StoreAlts)
        #f))

    ;; Return !nbdl.store possibly denoting an alternative
    ;; iff all the operands are singletons.
    (define (infer-visit-result Results)
      (define MemberName
        (let ((MN (value? !nbdl.member_name (car Results))))
          (if MN
            (get-member-name MN)
            #f)))
      (define Args
        (if MemberName
          (cdr Results)
          Results))
      (define StoreAlts
        (get-infer-args Args))
      (define Expr
        (cond
          ((not StoreAlts)
            #f)
          (MemberName
            (string-append (car StoreAlts) "." MemberName "("
                           (string-join (cdr StoreAlts) ", ")
                           ")"))
          (else
            (string-append (car StoreAlts) "("
                           (string-join (cdr StoreAlts) ", ")
                           ")"))))

      (if Expr
        (!nbdl.store (expr->type Expr))
        (!nbdl.store)))

    (define (build-visit MatchingResults? Loc Results)
      (define ResultType
        (if MatchingResults?
          (infer-visit-result Results)
          !nbdl.unit))
      (define VisitResult
        (result
          (create-op "nbdl.visit"
                     (loc: Loc)
                     (operands: Results)
                     (attributes:)
                     (result-types: ResultType))))
      (if MatchingResults?
        VisitResult
        (create-op "nbdl.discard"
                   (loc: Loc)
                   (operands: VisitResult)
                   (attributes:)
                   (result-types:))))

    (define (visit-aux MatchingResults? Loc ParamsSpec)
      (close-previous-scope)
      ;; This %expr is for the whole visit expr (ie its result).
      (if MatchingResults?
        (%expr Loc
          (lambda (Loc Fn)
            (%match-results
              ParamsSpec
              (lambda (Results)
                (Fn (build-visit MatchingResults? Loc Results))))))
        (%match-results
          ParamsSpec
          (lambda (Results)
            (build-visit MatchingResults? Loc Results)))))

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
         (let ()
           (define MatchingResults? matching-results?)
           (define CalleeLoc (syntax-source-loc Callee))
           (define ParamsSpec
             (list
               (%expr CalleeLoc
                 (lambda (Loc Fn)
                   (%match-expr+ Loc Callee Fn)))
               (%expr (syntax-source-loc StoreN)
                 (lambda (Loc Fn)
                   (%match-expr Loc StoreN Fn)))
               ...))
           (visit-aux matching-results? CalleeLoc ParamsSpec)
           ))))

    (define (infer-match-each-element ParamVals)
      ;; Since we will not likely support the
      ;; projection argument, we can get the result
      ;; of dereferencing the result of begin expr.
      (define Args
        (get-infer-args ParamVals))
      (define Begin
        (car Args))
      (if Begin
        (!nbdl.store
          (expr->type (string-append "*(" Begin ")")))
        (!nbdl.store)))

    (define (match-each-aux Begin End Fn)
      (define ParamsSpec (list Begin End))
      (%match-results ParamsSpec
        (lambda (ParamVals)
          (%top-level
            (lambda ()
              (define ElementT
                (infer-match-each-element ParamVals))
              (create-op "nbdl.match_each"
                         (loc: 0)
                         (operands: ParamVals)
                         (attributes:)
                         (result-types:)
                         (region: "body" ((Element : ElementT))
                            (Fn Element))))))))

    ;; Match each element of a range. (side effects only)
    (define-syntax match-each
      (syntax-rules ()
        ((match-each Range Fn)
         (match-each (visit '.begin Range)
                     (visit '.end Range)
                     Fn))
        ((match-each Begin End Fn)
          (match-each-aux (%expr (syntax-source-loc Begin)
                                 (lambda (Loc K)
                                   (%match-expr Loc Begin K)))
                          (%expr (syntax-source-loc End)
                                 (lambda (Loc K)
                                   (%match-expr Loc End K)))
                          Fn))))

    (define-syntax match-aux
      (syntax-rules (=>)
        ((match-aux PathSpec
          (TypeN => FnN) ...)
         (%match-path-spec PathSpec
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
                    (FnN OverloadArg)) ...))))))))

    ;; Match a resolved object by its type.
    ;; - It is an error if a type appears more that once as an alternative.
    ;;   (Think type switch)
    ;; - Each clause should be
    ;;    (<cpp-typename> => proc) or
    ;;    (else => proc)
    ;;   where proc is a unary lambda receiving the matched store.
    ;; - All Types should not have qualifiers.
    (define-syntax match
      (syntax-rules (else => store: key:)
        ((match PathSpec
          (else => DefaultFn))
         (match PathSpec
           ("" => DefaultFn)))
        ((match Path
          (Type1 => Fn1)
          (TypeN => FnN) ...
          (else => DefaultFn))
         (match Path
           (Type1 => Fn1)
           (TypeN => FnN) ...
           ("" => DefaultFn)))
        ; TODO Do we want this to be (%expr Store) to allow visit?
        ;      (Which would also require %match-results in match-aux)
        ((match Store
           (Type1 => Fn1)
           (TypeN => FnN) ...)
         (match-aux Store
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
      (create-op "nbdl.match_if"
                 (loc: Loc)
                 (operands: CondResult)
                 (attributes:)
                 (result-types:)
                 (region: "then" () (%top-level ThenThunk))
                 (region: "else" () (%top-level ElseThunk))))

    (define (match-if-impl Loc CondExprFn ThenThunk ElseThunk)
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
    ; If Else is not specified map it to C++ false
    ; for use as conditionals in cond clauses.
    (define-syntax match-if
      (syntax-rules ()
        ((match-if Cond Then)
         (match-if Cond Then 'false))
        ((match-if Cond Then Else)
         (match-if-impl (syntax-source-loc Cond)
                        (lambda (Loc Fn)
                          (%match-expr Loc Cond Fn))
                        (lambda () Then)
                        (lambda () Else)))))

    ; This is basically a copy of R7RS `cond` syntax
    ; adapted to use match-if.
    (define-syntax match-cond
      (syntax-rules (else =>)
        ((match-cond (else result1 result2 ...))
         (begin result1 result2 ...))
        ((match-cond (test => result))
         (let ((temp test))
           (match-if temp (result temp))))
        ((match-cond (test => result) clause1 clause2 ...)
         (let ((temp test))
           (match-if temp
             (result temp)
             (match-cond clause1 clause2 ...))))
        ((match-cond (test)) test)
        ((match-cond (test) clause1 clause2 ...)
         (let ((temp test))
           (match-if temp
             temp
             (match-cond clause1 clause2 ...))))
        ((match-cond (test result1 result2 ...))
         (match-if test (begin result1 result2 ...)))
        ((match-cond (test result1 result2 ...)
               clause1 clause2 ...)
         (match-if test
           (begin result1 result2 ...)
           (match-cond clause1 clause2 ...)))))

    ; FIXME Make this dump to error output.
    (define (dump-cpp name)
      (define Op
        (module-lookup module-cpp name))
      (translate-cpp Op)
      (flush-tokens)
      (newline))

    (define (dump-op name)
      (define Op
        (module-lookup module-cpp name))
      (dump Op)
      (newline))

    (define (dump-nbdl-module)
      (dump module-cpp))

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
    noop
    dump-cpp
    dump-op
    dump-nbdl-module
    ; reexport some base stuff
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
    dump
    reflect-match
    )
)  ; end of (nbdl spec)
