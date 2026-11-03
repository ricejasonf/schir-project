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
    (define define-llvm-wrapper
      (load-builtin "nbdl_spec_define_llvm_wrapper"))
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
    (define define-struct-op?
      (load-builtin "nbdl_spec_is_define_struct"))
    (define nbdl_spec_type_to_cpp_type
      (load-builtin "nbdl_spec_type_to_cpp_type"))
    (define nbdl_spec_cpp_alias_type
      (load-builtin "nbdl_spec_cpp_alias_type"))
    (define nbdl_spec_struct_type
      (load-builtin "nbdl_spec_struct_type"))
    (define nbdl_spec_struct_member_types
      (load-builtin "nbdl_spec_struct_member_types"))
    (define verify-recursive-visits
      (load-builtin "nbdl_spec_verify_recursive_visits"))

    ;; Create a !nbdl.cpp_alias type with a C++ typename as written.
    ;; The flatten pass replaces it with a !nbdl.cpp type
    ;; with the canonical typename.
    (define (!cpp Typename)
      (nbdl_spec_cpp_alias_type Typename))

    ;; Lift a string-like, named-store, or named-struct to its type
    ;; or return the mlir type as is.
    ;; Raise errors at the optional source location of T.
    (define (%cpp-or-type T . MaybeLoc)
      (cond
        ((or (string? T) (symbol? T)) (!cpp T))
        ((named-store? T) (named-store->cpp T))
        ((named-struct? T)
         (named-struct->type T (if (pair? MaybeLoc) (car MaybeLoc) #f)))
        (else T)))

    ;; Create a !nbdl.store type where string-likes are lifted to C++ types.
    (define (!nbdl.store . Alts)
      (apply nbdl_spec_create_store_type
             (map %cpp-or-type Alts)))

    ;; Lazily instantiate main module for all definitions.
    (define main-module #f)

    ;; Return the module of the current definitions.
    (define (%main-module)
      (unless main-module
        (set! main-module (create-top-module "nbdl_spec_module_cpp")))
      main-module)

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
              (memq Name export-c-internal-names)
              (memq Name export-shader-names))
        (namespace-prefix Name)
        Name))

    ;; The module of functions to be lowered by nbdl-lower.
    (define lower-module #f)

    ;; The module of functions lowered to LLVM.
    (define llvm-module #f)

    ;; Map each FuncOp to its copy in the lower-module as (FuncOp . Copy).
    (define %lower-module-copies '())

    ;; Copy a FuncOp into Module.
    (define (copy-to-module Module FuncOp)
      (with-module-builder
        Module
        (lambda () (copy-op FuncOp))))

    ;; Copy a FuncOp into the lower-module once.
    (define (add-to-lower-module FuncOp)
      (unless lower-module
        (set! lower-module (create-top-module "nbdl_spec_lower_module")))
      (unless (assq FuncOp %lower-module-copies)
        (set! %lower-module-copies
          (cons (cons FuncOp (copy-to-module lower-module FuncOp))
                %lower-module-copies))))

    ;; Get the copy of a FuncOp in the lower-module.
    (define (lower-module-copy FuncOp)
      (cdr (assq FuncOp %lower-module-copies)))

    ;; Get the llvm-module of the module being finalized.
    (define (finalize-llvm-module)
      (unless llvm-module
        (set! llvm-module (create-top-module "nbdl_spec_llvm_module")))
      llvm-module)

    ;; Copy the lower-module copy of a FuncOp into the llvm-module.
    (define (lower-to-llvm FuncOp CAdapter?)
      (let ((Copy (copy-to-module (finalize-llvm-module)
                                   (lower-module-copy FuncOp))))
        ;; Mark to have an abi adapter created to allow calling from C++.
        (when CAdapter?
          (mark-c-adapter Copy))
        Copy))

    ;; An element (Name Op) of an exported ops list.
    (define (%make-export Name Op)
      (list Name Op))

    (define (%export-op Export)
      (cadr Export))

    ;; Add the element (Name Op) to the end of ExportOps so
    ;; elements are in order of definition.
    (define (%add-export-op Loc ExportOps Name Op)
      (when (assq Name ExportOps)
        (error-with-loc Loc "exported name defined more than once: {}" Name))
      (append ExportOps (list (%make-export Name Op))))

    ;; Get each Op in ExportOps (in order of definition) that satisfies Pred.
    (define (%filter-exports ExportOps Pred)
      (let Loop ((Rest ExportOps) (Result '()))
        (if (null? Rest)
          (reverse Result)
          (let ((Op (%export-op (car Rest))))
            (Loop (cdr Rest) (if (Pred Op) (cons Op Result) Result))))))

    ;; Call Fn with each Op in ExportOps (in order of definition)
    ;; that satisfies Pred.
    (define (%finalize-exports ExportOps Pred Fn)
      (let loop ((Rest ExportOps))
        (unless (null? Rest)
          (let ((Op (%export-op (car Rest))))
            (when (Pred Op)
              (Fn Op)))
          (loop (cdr Rest)))))

    ;; Track functions exported via export-c, export-c-internal, or
    ;; export-shader that are lowered by finalize-module.
    (define %lower-ops '())

    ;; Track Structs exported similar to %lower-ops.
    (define %lowerable-struct-ops '())

    ;; Copy each struct definition into the lower-module
    ;; so nbdl-lower can convert struct types to tuples.
    (define (%copy-struct-ops StructOps)
      (let Loop ((Rest StructOps))
        (unless (null? Rest)
          (copy-to-module lower-module (car Rest))
          (Loop (cdr Rest)))))

    ;; Add a function to be lowered by finalize-module.
    (define (add-lower-op FuncOp)
      (set! %lower-ops (cons FuncOp %lower-ops)))

    ;; Return a procedure that emits C++ for an Op of Module via Translate.
    (define (emit-cpp Module Translate)
      (lambda (Op)
        (with-module-builder
          Module
          (lambda ()
            (Translate Op lexer-writer)
            (flush-tokens)))))

    (define (%function-op? Op)
      (not (define-struct-op? Op)))

    ;; Clear the module and exports so subsequent definitions
    ;; are added to a new module.
    (define (%reset-module)
      (set! main-module #f)
      (%reset-lower-modules)
      (set! export-cpp-names '())
      (set! export-c-names '())
      (set! export-c-internal-names '())
      (set! export-shader-names '())
      (set! export-cpp-ops '())
      (set! export-c-ops '())
      (set! export-c-internal-ops '())
      (set! export-shader-ops '())
      (set! %lower-ops '())
      (set! %lowerable-struct-ops '())
      (set! %write-lower-module? #f))

    ;; Clear the modules of the lowering stage and exit dialects.
    (define (%reset-lower-modules)
      (set! lower-module #f)
      (set! llvm-module #f)
      (set! %lower-module-copies '()))

    ;; Finish the module by writing c++ translations, forward declarations,
    ;; wrappers of functions lowered to LLVM, and lowering and injecting
    ;; backend modules.
    ;; Return the module and start a new one (even if finalizing fails.)
    (define (finalize-module)
      (define Module (%main-module))
      (define CppOps export-cpp-ops)
      (define COps export-c-ops)
      (define CInternalOps export-c-internal-ops)
      (define ShaderOps export-shader-ops)
      (define LowerOps %lower-ops)
      (define StructOps %lowerable-struct-ops)
      (define WriteLower? %write-lower-module?)
      (define (lower-op? Op) (memq Op LowerOps))
      (define FinalizeShaders #f)
      (%reset-module)
      (set! FinalizeShaders
        (%begin-shader-backend ShaderOps
                               (%filter-exports ShaderOps lower-op?)))
      (%finalize-exports CppOps define-struct-op?
                         (emit-cpp Module translate-cpp))
      ;; Declarations are written with canonical C++ types
      ;; to match the definitions.
      (nbdl_canonicalize_cpp_types Module current-schir-clang)
      ;; Infer types and simplify operations.
      (run-inference-passes Module)
      (%finalize-exports CppOps %function-op?
                         (emit-cpp Module declare-cpp))
      ;; Functions lowered to LLVM are called from C++ via wrappers.
      (%finalize-exports COps %function-op?
                         (emit-cpp Module define-llvm-wrapper))
      (%finalize-exports CppOps %function-op?
                         (emit-cpp Module translate-cpp))
      ;; Lower each function once for every exit dialect module.
      (%finalize-exports COps lower-op? add-to-lower-module)
      (%finalize-exports CInternalOps lower-op? add-to-lower-module)
      (%finalize-exports ShaderOps lower-op? add-to-lower-module)
      (when lower-module
        (%copy-struct-ops StructOps)
        (apply run-passes lower-module "nbdl-lower" %lowering-passes)
        (when WriteLower?
          (write lower-module)
          (newline)))
      (%finalize-exports COps lower-op?
                         (lambda (Op) (lower-to-llvm Op #t)))
      (%finalize-exports CInternalOps lower-op?
                         (lambda (Op) (lower-to-llvm Op #f)))
      ;; The shader backend may add functions to the llvm-module.
      (when FinalizeShaders
        (FinalizeShaders Module))
      (when llvm-module
        (run-passes llvm-module "nbdl-to-llvm" "nbdl-c-adapter")
        (inject-module llvm-module))
      (%reset-lower-modules)
      Module)

    (define %write-lower-module? #f)

    ;; Write the lower-module of finalize-module before lowering it
    ;; to the exit dialects (for debugging and testing.)
    (define (write-lower-module)
      (set! %write-lower-module? #t))

    ;; The procedure registered via register-shader-backend or #f.
    (define %shader-backend #f)

    ;; Register the backend that lowers the functions exported via
    ;; export-shader where Begin is called by finalize-module with the
    ;; lowered functions (in order of definition), saves and resets the
    ;; state of the backend, and returns a procedure that receives the
    ;; finalized module after nbdl-lower and before the llvm-module is
    ;; lowered.
    (define (register-shader-backend Begin)
      (when (and %shader-backend (not (eq? %shader-backend Begin)))
        (error "a shader backend is already registered"))
      (set! %shader-backend Begin))

    ;; Call the Begin procedure of the shader backend or raise an error if
    ;; there are functions exported via export-shader without one.
    (define (%begin-shader-backend ShaderExports ShaderOps)
      (cond
        (%shader-backend (%shader-backend ShaderOps))
        ((null? ShaderExports) #f)
        (else
         (error-with-loc (%export-op (car ShaderExports))
                         (string-append "export-shader requires a shader"
                                        " backend such as (nbdl spec spirv): {}")
                         (car (car ShaderExports))))))

    ;; Thunk receives the symbol name and should return
    ;; a new top level operation using the current module builder.
    ;; Initial passes and C++ translation are deferred to finalize-module.
    (define (top-level-op Loc Name Thunk)
      (%check-exports Loc Name)
      (let ((TopLevelOp (%create-top-level-op Loc (top-level-name Name)
                                               Thunk)))
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
        ;; A function may be exported to a shader in addition to C.
        (when (memq Name export-shader-names)
          (set! export-shader-ops
            (%add-export-op Loc export-shader-ops Name TopLevelOp)))
        TopLevelOp))

    ;; Raise an error if Name is exported in conflicting ways.
    (define (%check-exports Loc Name)
      (when (and (memq Name export-cpp-names)
                 (memq Name export-c-names))
        (error-with-loc Loc "name cannot be exported to both C++ and C: {}"
                        Name))
      (when (and (memq Name export-c-internal-names)
                 (or (memq Name export-cpp-names)
                     (memq Name export-c-names)))
        (error-with-loc Loc "internal name cannot also be exported: {}"
                        Name))
      (when (and (memq Name export-cpp-names)
                 (memq Name export-shader-names))
        (error-with-loc Loc
                        "name cannot be exported to both C++ and a shader: {}"
                        Name)))

    ;; Denote that the Thunk of a top level operation is running.
    (define %in-top-level-op #f)

    ;; Create and verify a top level operation via Thunk
    ;; which receives the symbol name.
    ;; Raise an error if it is nested within another top level operation.
    (define (%create-top-level-op Loc SymName Thunk)
      (when %in-top-level-op
        (error-with-loc Loc "definition must be at the top level"))
      (dynamic-wind
        (lambda () (set! %in-top-level-op #t))
        (lambda ()
          (with-module-builder
            (%main-module)
            (lambda ()
              (define TopLevelOp (Thunk SymName))
              ;; The verify pass may also raise a more specific error.
              (verify TopLevelOp)
              TopLevelOp)))
        (lambda () (set! %in-top-level-op #f))))

    ;; Define the value of a definition that visit raises an error for
    ;; with a format string Message and Name.
    (define-record-type <uncallable>
      (make-uncallable Message Name)
      uncallable?
      (Message uncallable-message)
      (Name uncallable-name))

    ;; Create the symbol name for a match-fn.
    ;; Suffix the name of a match-fn exported to C++ so it is
    ;; distinct from the name of its wrapper. (see define-match-fn)
    (define (%match-fn-name Name)
      (if (memq Name export-cpp-names)
        (string-append (namespace-prefix Name) ".match_fn")
        Name))

    ;; Create a nbdl.define_match_fn as a top level operation
    ;; via Thunk which receives the symbol name.
    (define (%top-level-match-fn Loc Name Thunk)
      (%check-exports Loc Name)
      (when (or (memq Name export-c-names)
                (memq Name export-c-internal-names))
        (error-with-loc Loc "match-fn cannot be exported to C: {}" Name))
      (when (memq Name export-shader-names)
        (error-with-loc Loc "match-fn cannot be exported to a shader: {}"
                        Name))
      (%create-top-level-op Loc (%match-fn-name Name) Thunk))

    (define !nbdl.member_name (type "!nbdl.member_name"))
    (define !nbdl.func_name (type "!nbdl.func_name"))
    (define !nbdl.struct_name (type "!nbdl.struct_name"))
    (define !nbdl.closure (type "!nbdl.closure"))
    (define !nbdl.unit (type "!nbdl.unit"))
    (define !nbdl.unknown (type "!nbdl.unknown"))
    (define i32 (type "i32"))
    (define f32 (type "f32"))

    ;; Not used types
    ; (define !nbdl.tag (type "!nbdl.tag")) ; Not used.
    ; (define !nbdl.empty (type "!nbdl.empty"))

    ;; Define a thunk that should receive a location and callback
    ;; to resolve a value once its dependencies are resolved.
    ;; (e.g. arguments to visit)
    ;; Additionally, an optional MatchImpl may be provided
    ;; to specify how the result is matched given a procedure
    ;; taking the following arguments:
    ;;   MatchImpl: (Loc Store Key Fn) => ()
    ;; where Store is the Expr sans the MatchImpl.
    (define-record-type <nbdl-expr>
      (%make-expr Loc Thunk MatchImpl)
      expr?
      (Loc expr-loc)
      (Thunk expr-thunk)
      (MatchImpl expr-match-impl))

    (define-syntax %expr
      (syntax-rules ()
        ;; Thunk: (Loc Fn) -> Store
        ((%expr Loc Thunk)
         (%make-expr Loc Thunk #f))
        ((%expr Loc Thunk MatchImpl)
         (%make-expr Loc Thunk MatchImpl))))

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

    ;; Invoke the Thunk created with %expr
    (define (%invoke-expr Expr Fn)
      (unless (expr? Expr)
        (error "expecting %expr: {}" Expr))
      ; TODO If MatchImpl is set, maybe create a new type?
      (when (expr-match-impl Expr)
        (error "MatchImpl is not unwrapped: {}" Expr))
      ((expr-thunk Expr) (expr-loc Expr) Fn))

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
        ((named-struct? Expr)
         (Fn (build-struct-name Loc Expr)))
        ((uncallable? Expr)
         (error-with-loc Loc (uncallable-message Expr)
                         (uncallable-name Expr)))
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
      (%make-discard Loc))

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

    ;; Build a nbdl.construct of an object of type Typename.
    (define (build-construct Loc Typename InitArgs)
      (result
        (create-op
          "nbdl.construct"
          (loc: Loc)
          (operands: InitArgs)
          (attributes:)
          (result-types: (%cpp-or-type Typename Loc))
          )))

    (define (construct-aux Loc Typename InitArgExprs)
      (%expr
        Loc
        (lambda (Loc Fn)
          (%match-results
            InitArgExprs
            (lambda (InitArgs)
              (Fn (build-construct Loc Typename InitArgs)))))))

    ;; Construct an object of type Typename.
    (define-syntax construct
      (syntax-rules ()
        ((construct Typename InitArgN ...)
         (construct-aux
           (syntax-source-loc Typename)
           Typename
           (list (%single-expr InitArgN) ...)))))

    ;; Construct an object lifted to a store.
    (define-syntax store
      (syntax-rules (init-args:)
        ((store Typename)
         (store Typename (init-args:)))
        ((store Typename (init-args: InitArgN ...))
         (lift-store-aux
           (syntax-source-loc Typename)
           (construct Typename InitArgN ...)))))

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
         (%define-store make-named-store Name (InitParams ...)
           StoreFunctionalN ...))))

    ;; Define Name as the result of Make applied to the symbol name
    ;; and the nbdl.define_struct.
    (define-syntax %define-store
      (syntax-rules ()
        ((%define-store Make Name (InitParams ...) StoreFunctionalN ...)
         (define Name
           (let ((SymName #f))
            (let ((DefineStructOp
                   (top-level-op
                     (syntax-source-loc Name)
                     'Name
                     (lambda (SymName_)
                       (define Loc (syntax-source-loc Name))
                       (set! SymName SymName_)
                       (create-op
                         "nbdl.define_struct"
                         (loc: Loc)
                         (operands:)
                         (attributes: ("sym_name" (string-attr SymName)))
                         (result-types:)
                         (region: "body" ((InitParams : !nbdl.unknown) ...)
                                  (define-store-aux
                                    Loc
                                    (lambda (ProcessBody)
                                      ;; Ensure nonempty lambda.
                                      (ProcessBody StoreFunctionalN) ... #t)
                                    )))))))
             (Make SymName DefineStructOp)))))))

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

    ;; Pass through a StoreFunctional of a struct member
    ;; if Key is a member name.
    (define (%struct-member Loc Key StoreFunctional)
      (unless (member-name-literal? Key)
        (error-with-loc Loc "expecting a member name as a struct key: {}" Key))
      StoreFunctional)

    ;; Define a struct where each key is a member name.
    (define-syntax define-struct
      (syntax-rules (member: init-args:)
        ((define-struct Name (Formals ...)
            (member: KeyN TypenameN (init-args: InitArgsNN ...)) ...)
         (%define-store
           (lambda (SymName DefineStructOp)
             (define CppVisible (and (memq 'Name export-cpp-names) #t))
             (unless CppVisible
               (set! %lowerable-struct-ops
                 (cons DefineStructOp %lowerable-struct-ops)))
             (make-named-struct SymName DefineStructOp CppVisible))
           Name (Formals ...)
           (%struct-member
             (syntax-source-loc KeyN)
             KeyN
             (store-compose KeyN (store TypenameN (init-args: InitArgsNN ...))))
           ...
           ))))

    ;; Denote the name of a store defined as top level operation
    ;; in the module.
    (define-record-type <named-store>
      (make-named-store SymName DefineStructOp)
      named-store?
      (SymName named-store-sym-name)
      (DefineStructOp named-store-op))

    ;; Convert a named-store to a c++ type.
    (define (named-store->cpp NamedStore)
      (!cpp (named-store-sym-name NamedStore)))

    ;; Denote the name of a struct defined via define-struct
    ;; where CppVisible is true if it is exported to C++.
    (define-record-type <named-struct>
      (%make-named-struct SymName DefineStructOp CppVisible Module)
      named-struct?
      (SymName named-struct-sym-name)
      (DefineStructOp named-struct-op)
      (CppVisible named-struct-cpp-visible?)
      (Module named-struct-module))

    (define (make-named-struct SymbolName DefineStructOp CppVisible)
      (%make-named-struct SymbolName DefineStructOp CppVisible
                          (%main-module)))

    ;; Get the types of the members of a named-struct in order of definition.
    (define (struct-member-types NamedStruct)
      (nbdl_spec_struct_member_types (named-struct-op NamedStruct)))

    ;; Map a named-struct to a C++ type if it is visible to C++
    ;; or to a !nbdl.struct type otherwise.
    ;; Raise errors at Loc unless it is #f.
    (define (named-struct->type NamedStruct Loc)
      (define SymName (named-struct-sym-name NamedStruct))
      (define Module (named-struct-module NamedStruct))
      (define Message "struct is not defined: {}")
      (cond
        ((named-struct-cpp-visible? NamedStruct) (!cpp SymName))
        ;; Reject a struct that is not in the module.
        ((and (not (eq? Module main-module)) Loc)
         (error-with-loc Loc Message SymName))
        ((not (eq? Module main-module))
         (error Message SymName))
        (else (nbdl_spec_struct_type SymName))))

    (define (build-struct-name Loc NamedStruct)
      (result (create-op
                "nbdl.struct_name"
                (loc: Loc)
                (operands:)
                (attributes:
                  ("name" (flat-symbolref-attr
                            (named-struct-sym-name NamedStruct))))
                (result-types: !nbdl.struct_name))))

    ;; A 'named-fn' can be used in an expr+
    ;; where FnOp is a func.func or nbdl.define_match_fn.
    (define-record-type <named-fn>
      (%make-named-fn SymName FnOp Loc)
      named-fn?
      (SymName named-fn-sym-name)
      (FnOp named-fn-op)
      (Loc named-fn-loc))

    (define (make-named-fn SymbolName FnOp)
      (%make-named-fn SymbolName FnOp (source-loc FnOp)))

    ;; Let a function refer to itself within its own body before the
    ;; FnOp exists.
    (define (%make-self-named-fn SymbolName Loc)
      (%make-named-fn SymbolName #f Loc))

    (define (build-func-name NamedFn)
      (define Name (named-fn-sym-name NamedFn))
      (define Loc (named-fn-loc NamedFn))
      (result (create-op
                "nbdl.func_name"
                (loc: Loc)
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
          (if (expr? RootStore)
            (%invoke-expr RootStore
              (lambda (Root)
                (%match-path-root Root PathNodes Fn)))
            (%match-path-root RootStore PathNodes Fn))))
        (else (error "expecting nbdl pathspec: {}" PathSpec))))

    ;; Match the PathNodes of a path starting at its root.
    (define (%match-path-root RootStore PathNodes Fn)
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
          (error "expecting a root store object in path: {}" RootStore))))

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
          ((or (value? path) (expr? path))
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

    ;; Define the result of an expression that is discarded at Loc.
    (define-record-type <nbdl-discard>
      (%make-discard Loc)
      discarded?
      (Loc %discard-loc))

    (define (build-discard Loc Value)
      (create-op "nbdl.discard"
                   (loc: Loc)
                   (operands: Value)
                   (attributes:)
                   (result-types:))
      (%make-discard Loc))

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

    (define (discarded-loc Obj)
      (and (discarded? Obj) (%discard-loc Obj)))

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
          (%make-discard Loc))))

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
              (error-with-loc (expr-loc PathSpecExpr)
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

    ;; Terminate a region that has nothing to do.
    (define (build-discard-unit Loc)
      (build-discard Loc (build-unit)))

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
    ; Discard a unit value if Else is not specified.
    (define-syntax match-if
      (syntax-rules ()
        ((match-if Cond Then)
         (match-if-aux (syntax-source-loc Cond)
                       (lambda (Loc Fn)
                         (%match-expr Loc Cond Fn))
                       (lambda (ThenArg) Then)
                       (lambda ()
                         (build-discard-unit (syntax-source-loc Cond)))))
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
                       (lambda () (build-discard-unit (syntax-source-loc test)))))
        ((match-cond (test => result) clause1 clause2 ...)
         (match-if-aux (syntax-source-loc test)
                       (lambda (Loc Fn) (%match-expr Loc test Fn))
                       (lambda (ThenArg) ((make-visit-proc result) ThenArg))
                       (lambda () (match-cond clause1 clause2 ...))))
        ((match-cond (test)) test)
        ((match-cond (test) clause1 clause2 ...)
         (match-cond (test => (lambda (DiscardMe)
                                (build-discard-unit (syntax-source-loc test))))
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
    ;; Inline every visit on the function and inline recursive tail
    ;; visits as a loop.
    ;; If Name is exported to C++, define a wrapper function with
    ;; the exported name that visits the match-fn with each raw
    ;; parameter lifted to a store.
    (define-syntax define-match-fn
      (syntax-rules ()
        ((define-match-fn Name (Arg ...) Body ...)
         (define Name
           (let ()
            (define Loc (syntax-source-loc Name))
            (define SymName (%match-fn-name 'Name))
            (define MatchFn
              (make-named-fn
                SymName
                (%top-level-match-fn
                  Loc
                  'Name
                  (lambda (SymName_)
                    (define Op
                      ;; Let Name refer to the function in its body.
                      (let ((Name (%make-self-named-fn SymName_ Loc)))
                        (create-op
                          "nbdl.define_match_fn"
                          (loc: Loc)
                          (operands:)
                          (attributes:
                            ("sym_name" (string-attr SymName_)))
                          (result-types:)
                          (region: "body" ((Arg : (!nbdl.store)) ...)
                                   Body ...))))
                    (verify-recursive-visits Op)
                    Op))))
            (when (memq 'Name export-cpp-names)
              (top-level-op
                Loc
                'Name
                (lambda (WrapperName)
                  (create-op
                    "func.func"
                    (loc: Loc)
                    (operands:)
                    (attributes:
                      ("sym_name" (string-attr WrapperName))
                      ("function_type"
                       (type-attr
                         (%function-type
                           (make-vector (length '(Arg ...)) !nbdl.unknown)
                           #()))))
                    (result-types:)
                    (region: "body" ((Arg : !nbdl.unknown) ...)
                             (visit MatchFn (lift-store Arg) ...))))))
            MatchFn)))))

    ;; Declare match-fns so a function may visit them before they are
    ;; defined. (e.g. Define mutually recursive functions.)
    (define-syntax declare-match-fn
      (syntax-rules ()
        ((declare-match-fn Name ...)
         (begin
           (define Name (%make-self-named-fn (%match-fn-name 'Name)
                                             (syntax-source-loc Name)))
           ...))))

    ;; Create a closure as an expr whose body captures the values in
    ;; scope with the same invocable semantics as define-match-fn.
    (define-syntax match-fn
      (syntax-rules ()
        ((match-fn (Arg ...) Body1 BodyN ...)
         (%expr
           (syntax-source-loc Body1)
           (lambda (Loc Fn)
             (Fn (result
                   (create-op
                     "nbdl.closure"
                     (loc: Loc)
                     (operands:)
                     (attributes:)
                     (result-types: !nbdl.closure)
                     (region: "body" ((Arg : (!nbdl.store)) ...)
                              (%top-level
                                (lambda () Body1 BodyN ...)))))))))))

    ;; Define a normal function whose parameter and result types are
    ;; mlir types where string-likes are lifted to C++ types.
    ;; Use lift-store on a parameter to use it with Nbdl operations.
    ;; A result type of !nbdl.unknown is inferred from the returned values
    ;; by the inference passes. (see register-inference-pass)
    ;; An optional (with-return K) clause before the body binds K to a
    ;; continuation where (visit K Expr ...) is (return Expr ...)
    ;; so it may be passed to match-fns that end the function.
    ;; Each visit on K must be in tail position.
    (define-syntax define-fn
      (syntax-rules (: -> with-return)
        ((define-fn Name ((Arg : ArgT) ...) -> (RetT ...)
           (with-return K) Body1 BodyN ...)
         (%define-fn Name ((Arg : ArgT) ...) (RetT ...) (K)
           Body1 BodyN ...))
        ((define-fn Name ((Arg : ArgT) ...) -> (RetT ...) Body1 BodyN ...)
         (%define-fn Name ((Arg : ArgT) ...) (RetT ...) ()
           Body1 BodyN ...))))

    ;; Define a function for define-fn binding each K to a return closure.
    (define-syntax %define-fn
      (syntax-rules (:)
        ((%define-fn Name ((Arg : ArgT) ...) (RetT ...) (K ...)
           Body1 BodyN ...)
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
                                (vector (%cpp-or-type
                                          ArgT (syntax-source-loc ArgT)) ...)
                                (vector (%cpp-or-type
                                          RetT (syntax-source-loc RetT))
                                        ...)))))
                         (result-types:)
                         (region: "body" ((Arg : (%cpp-or-type ArgT))
                                          ...)
                                  (let ((K (%return-closure
                                             (syntax-source-loc K)
                                             SymName))
                                        ...)
                                    Body1 BodyN ...)))))))
             ;; Process exports if any.
             (when (or (memq 'Name export-c-names)
                       (memq 'Name export-c-internal-names)
                       (memq 'Name export-shader-names))
               (add-lower-op FuncOp))
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

    (define (build-get-parent Loc Store)
      (unless (store-value? Store)
        (error-with-loc Loc "expecting a store to get-parent (see lift-store)"))
      (result
        (create-op "nbdl.get_parent"
                   (loc: Loc)
                   (operands: Store)
                   (attributes:)
                   (result-types: (!nbdl.store)))))

    ;; Get the parent node of a store as an expr.
    (define-syntax get-parent
      (syntax-rules ()
        ((get-parent Store)
         (%expr (syntax-source-loc Store)
                (lambda (Loc Fn)
                  (%match-expr Loc Store
                    (lambda (Value)
                      (Fn (build-get-parent Loc Value)))))))))

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

    ;; Return the results (if any) from a function defined with define-fn.
    (define-syntax return
      (syntax-rules ()
        ((return)
         (return-aux (current-source-loc) '()))
        ((return Expr1 ExprN ...)
         (return-aux (syntax-source-loc Expr1)
                     (list (%single-expr Expr1) (%single-expr ExprN) ...)))))

    ;; Create a closure as an expr that returns its arguments from the
    ;; function named SymName.
    (define (%return-closure Loc SymName)
      (%expr Loc
             (lambda (Loc Fn)
               (Fn (result
                     (create-op "nbdl.return_closure"
                                (loc: Loc)
                                (operands:)
                                (attributes:
                                  ("function" (flat-symbolref-attr SymName)))
                                (result-types: !nbdl.closure)))))))

    ;; Reject a with-return clause with a nice error message.
    (define-syntax with-return
      (syntax-rules ()
        ((with-return Form ...)
         (error-with-loc (syntax-source-loc)
           "unexpected with-return"))))

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
            (named-fn-op Name))
          ((named-store? Name)
            (named-store-op Name))
          ((named-struct? Name)
            (named-struct-op Name))
         (else (module-lookup (%main-module) Name))))
      (translate-cpp Op)
      (newline))

    ;; Map a mlir.type to a !nbdl.cpp type with a canonical
    ;; C++ typename or #f if the type is not mappable to C++.
    (define (type->cpp T)
      (nbdl_spec_type_to_cpp_type T current-schir-clang))

    (define (dump-op name)
      (define Op
        (module-lookup (%main-module) name))
      (dump Op)
      (newline))

    (define (dump-nbdl-module)
      (dump (%main-module)))

    ;; Write the Module (ie as returned by finalize-module)
    ;; or the module of the current definitions.
    (define (write-nbdl-module . Module)
      (write (if (null? Module) (%main-module) (car Module)))
      (newline))

    ;; Track the names of exports.
    (define export-cpp-names '())
    (define export-c-names '())
    (define export-c-internal-names '())
    (define export-shader-names '())

    ;; Return true if Name is exported as Kind
    ;; ('cpp, 'c, 'c-internal, or 'shader.)
    (define (exported-as? Name Kind)
      (and (memq Name (case Kind
                        ((cpp) export-cpp-names)
                        ((c) export-c-names)
                        ((c-internal) export-c-internal-names)
                        ((shader) export-shader-names)
                        (else (error "unknown export kind: {}" Kind))))
           #t))

    ;; Track exported ops as (Name Op) in order of definition.
    (define export-cpp-ops '())
    (define export-c-ops '())
    (define export-c-internal-ops '())
    (define export-shader-ops '())

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

    ;; Export functions to the backend registered via
    ;; register-shader-backend (e.g. by (nbdl spec spirv).)
    (define-syntax export-shader
      (syntax-rules ()
        ((export-shader Name ...)
         (set! export-shader-names
           (append
             export-shader-names
             (list 'Name ...))))))

    (define (run-pass-nbdl-flatten)
      (nbdl_run_flatten_pass
        (%main-module) current-schir-clang))

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
    ;; dialects to dialects that nbdl-to-llvm and nbdl-to-spirv can lower.
    (define (register-lowering-pass Pipeline)
      (unless (string? Pipeline)
        (error "expecting pass pipeline string: {}" Pipeline))
      (unless (member Pipeline %lowering-passes)
        (set! %lowering-passes
          (append %lowering-passes (list Pipeline)))))

    ;; Run the flatten pass and the registered inference passes
    ;; on the Module or the module of the current definitions.
    ;; It is an error if the result type of any function is not inferred.
    (define (run-inference-passes . Module)
      (apply nbdl_run_inference_passes
             (if (null? Module) (%main-module) (car Module))
             current-schir-clang %inference-passes))

  ) ; end of... begin
  (export
    define-context
    define-store
    define-struct
    store-compose
    variant
    store
    construct
    get
    match
    match-cond
    match-each
    match-if
    declare-match-fn
    define-match-fn
    match-fn
    define-fn
    lift-store
    get-parent
    return
    with-return
    register-inference-pass
    register-lowering-pass
    match-params
    visit
    sfinae-visit
    noop
    export-cpp
    export-c
    export-c-internal
    export-shader
    finalize-module
    write-lower-module

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

    ;; Stuff for backends (e.g. (nbdl spec spirv))
    register-shader-backend
    add-lower-op
    exported-as?
    make-uncallable
    lower-module-copy
    copy-to-module
    finalize-llvm-module
    emit-cpp
    named-struct?
    named-struct-sym-name
    struct-member-types
    mark-c-adapter
    define-llvm-wrapper
    %cpp-or-type
    )
)  ; end of (nbdl spec)
