(import (schir base))

(define-library (geomalg base)
  (export
    geomalg-current-module
    geomalg-module-init
    define-func
    basis-vector-type
    blade-type
    multivector-type
    scalar e1 e2 e3 ni no ;; Conformal GA basis vectors
    ; k-blade ;; TODO make literal op?
    sum
    outprod
    inprod
    gprod
    rev
    inv)
  (import (schir base)
          (schir mlir))
  (begin
    (load-plugin "libGeomalg.so")
    ;; Take a tag that is a power of two (not including the sign bit).
    (define basis-vector-type
      (load-builtin "geomalg_basis_vector_type"))
    ;; Each argument is a non-empty list of basis vectors
    ;; where the sign is determined by the order.
    ;; Alternatively, accept a raw tag value.
    (define blade-type
      (load-builtin "geomalg_blade_type"))
    ;; Take a nonempty list of blade-types.
    ;; Sort terms by tag value.
    (define multivector-type
      (load-builtin "geomalg_multivector_type"))
    ;; Register the geomalg dialect and such.
    (define geomalg-init
      (load-builtin "geomalg_init"))
    (define sum-impl
      (load-builtin "geomalg_sum_impl"))
    (define geomalg-current-module
      (load-builtin "geomalg_current_module"))
    (geomalg-init)
    (load-dialect "geomalg")

    ;; Initialize a module and set it as current module.
    (define (geomalg-module-init name)
      (let ((ModuleOp
              (create-op "builtin.module"
                         (loc: 0)
                         (operands:)
                         (attributes: ("sym_name" name))
                         (result-types:)
                         (region: "body" () 0)))) ;; Just create the region.
        ; Set the current insertion point to the region body.
        (at-block-begin (entry-block (get-region ModuleOp)))
        ModuleOp))

    ;; Just initialize a monolithic module.
    (set! geomalg-current-module
      (geomalg-module-init "geomalg_main"))

    ;; If any function parameter type is unknown
    ;; then the func is used as a template.
    (define !geomalg.unknown (type "!geomalg.unknown"))

    ;; Go full 5-d Conformal Geometric Algebra since
    ;; everything we want is a subalgebra of that.
    (define scalar (basis-vector-type 0))
    (define e1 (basis-vector-type 1))
    (define e2 (basis-vector-type 2))
    (define e3 (basis-vector-type 4))
    (define no (basis-vector-type 8))
    (define ni (basis-vector-type 16))

    (define vec2 (multivector-type e1 e2))
    (define vec3 (multivector-type e1 e2 e3))
    (define uvec2 (multivector-type e1 e2 e3))
    (define uvec3 (multivector-type e1 e2 e3))

    (define (define-func-impl Loc ReturnLoc FuncName ArgTypes ArgLocs BodyFn)
      (define FuncOp
        (create-op "func.func"
                   (loc: Loc)
                   (operands:)
                   (attributes:
                     ("sym_name" (string-attr FuncName))
                     ("function_type"
                       (type-attr (%function-type
                                   (apply vector ArgTypes)
                                   #(!geomalg.unknown)))))
                   (result-types:)
                   (region: "body" () 0)))
      (with-builder (lambda ()
        (define Block (entry-block FuncOp))
        (define (AddArg ArgType ArgLoc)
          (add-argument Block ArgType ArgLoc))
        (define BlockArgs
          (map AddArg ArgTypes ArgLocs))
        (at-block-begin Block)
        (let ((Result (apply BodyFn BlockArgs)))
          (create-op "geomalg.return"
                     (loc: ReturnLoc)
                     (operands: Result)
                     (attributes:)
                     (result-types:)))
        (if #f #f) ;; Return undefined.
        )))

    (define-syntax define-func
      (syntax-rules()
        ((define-func FuncName ((ArgName : ArgType) ...)
                      BodyExprI ... BodyExprN)
         (define-func-impl (syntax-source-loc FuncName)
                           (syntax-source-loc BodyExprN)
                           'FuncName
                           (list ArgType ...)
                           (list (syntax-source-loc ArgName) ...)
                           (lambda (ArgName ...)
                             BodyExprI
                             ...
                             BodyExprN)))))

    (define-syntax sum
      (syntax-rules ()
        ((sum V1 VN ...)
         (sum-impl (syntax-source-loc V1) V1 VN ...))))

    ;; TODO It would be nice to make a syntax to generate these
    ;;      but local syntax is not yet supported in schir-scheme.
    ;;      The impl functions prevent syntax garbage creation.

    (define (outprod-impl Loc V1 V2)
      (result (create-op "geomalg.outprod"
                         (loc: Loc)
                         (operands: V1 V2)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax outprod
      (syntax-rules ()
        ((outprod V1 V2)
         (outprod-impl (syntax-source-loc V1) V1 V2))))

    (define (inprod-impl Loc V1 V2)
      (result (create-op "geomalg.inprod"
                         (loc: Loc)
                         (operands: V1 V2)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax inprod
      (syntax-rules ()
        ((inprod V1 V2)
         (inprod-impl (syntax-source-loc V1) V1 V2))))

    (define (gprod-impl Loc V1 V2)
      (result (create-op "geomalg.gprod"
                         (loc: Loc)
                         (operands: V1 V2)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax gprod
      (syntax-rules ()
        ((gprod V1 V2)
         (gprod-impl (syntax-source-loc V1) V1 V2))))

    (define (rev-impl Loc V)
      (result (create-op "geomalg.rev"
                         (loc: Loc)
                         (operands: V)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax rev
      (syntax-rules ()
        ((rev V)
         (rev-impl (syntax-source-loc V) V))))

    (define (inv-impl Loc V)
      (result (create-op "geomalg.inv"
                         (loc: Loc)
                         (operands: V)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax inv
      (syntax-rules ()
        ((inv V)
         (inv-impl (syntax-source-loc V) V))))
    )) ;; define-library
