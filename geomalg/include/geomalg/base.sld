(import (schir base))

(define-library (geomalg base)
  (export
    geomalg-current-module
    geomalg-module-init
    define-func
    !basis-vector
    !blade
    !multivector
    !scalar !e1 !e2 !e3 !no !ni ;; Conformal GA basis vectors (types)
    scalar e1 e2 e3 no ni ;; Conformal GA basis vectors
    !zero
    !vec2 !vec3 !uvec2 !uvec3
    sum
    oprod
    iprod
    gprod
    vprod
    dot
    negate
    rev
    grade-invo
    inverse
    convert
    expand
    )
  (import (schir base)
          (schir mlir))
  (begin
    (load-plugin "libGeomalg.so")
    ;; Take a tag that is a power of two (not including the sign bit).
    (define !basis-vector
      (load-builtin "geomalg_basis_vector_type"))
    ;; Each argument is a non-empty list of basis vectors
    ;; where the sign is determined by the order.
    ;; Alternatively, accept a raw tag value.
    (define !blade
      (load-builtin "geomalg_blade_type"))
    ;; Take a nonempty list of !blades.
    ;; Sort terms by tag value.
    (define !multivector
      (load-builtin "geomalg_multivector_type"))
    ;; Register the geomalg dialect and such.
    (define geomalg-init
      (load-builtin "geomalg_init"))
    #;(define sum-impl
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
    (define !zero (type "!geomalg.zero"))
    (define !f32 (type "f32"))

    ;; Go full 5-d Conformal Geometric Algebra since
    ;; everything we want is a subalgebra of that.
    (define !scalar (!basis-vector 0))
    (define !e1 (!basis-vector 1))
    (define !e2 (!basis-vector 2))
    (define !e3 (!basis-vector 4))
    (define !no (!basis-vector 8))
    (define !ni (!basis-vector 16))

    (define !vec2 (!multivector !e1 !e2))
    (define !vec3 (!multivector !e1 !e2 !e3))
    (define !uvec2 (!multivector !e1 !e2 !e3))
    (define !uvec3 (!multivector !e1 !e2 !e3))

    (define !vec4 (!multivector !e1 !e2 !e3 !no))
    (define !vec5 (!multivector !e1 !e2 !e3 !no !ni))

    ; Args are checked within a pass.
    (define-syntax %define-call-fn
      (syntax-rules ()
        ((%define-call-fn (FuncName ArgN ...))
         (define (FuncName ArgN ...)
           (result
             (create-op "geomalg.call"
                        (loc: (syntax-source-loc FuncName))
                        (operands: ArgN ...)
                        (attributes:
                          ("callee" (flat-symbolref-attr 'FuncName)))
                        (result-types: !geomalg.unknown)))))))

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
         (begin
           (%define-call-fn (FuncName ArgName ...))
           (define-func-impl (syntax-source-loc FuncName)
                             (syntax-source-loc BodyExprN)
                             'FuncName
                             (list ArgType ...)
                             (list (syntax-source-loc ArgName) ...)
                             (lambda (ArgName ...)
                               BodyExprI
                               ...
                               BodyExprN))))))

    ; Shorcut to create ops with result only specifying
    ; operands and inferring result type.
    (define (%create-val OpName Loc . Operands)
      (result
        (create-op OpName
                   (loc: Loc)
                   (operands: Operands)
                   (attributes:))))

    (define-syntax sum
      (syntax-rules ()
        ((sum)
         (%create-blade-val "geomalg.blade"
                            !zero '() 0))
        ((sum V1 VN ...)
         (%create-val "geomalg.sum"
                      (syntax-source-loc V1)
                      V1 VN ...))))

    (define-syntax oprod
      (syntax-rules ()
        ((oprod V1 V2)
         (%create-val "geomalg.oprod"
                      (syntax-source-loc V1)
                      V1 V2))))

    (define-syntax iprod
      (syntax-rules ()
        ((iprod V1 V2)
         (%create-val "geomalg.iprod"
                      (syntax-source-loc V1)
                      V1 V2))))

    (define (dot-impl Loc V1 V2)
      (result (create-op "geomalg.dot"
                         (loc: Loc)
                         (operands: V1 V2)
                         (attributes:)
                         (result-types: !scalar))))

    (define-syntax dot
      (syntax-rules ()
        ((dot V1 V2)
         (dot-impl (syntax-source-loc V1)
                   V1 V2))))

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

    (define (vprod-impl Loc Arg Versors)
      (result (create-op "geomalg.vprod"
                         (loc: Loc)
                         (operands: Arg Versors)
                         (attributes:)
                         (result-types: !geomalg.unknown))))

    (define-syntax vprod
      (syntax-rules ()
        ((vprod Arg Versors ...)
         (vprod-impl (syntax-source-loc Arg)
                     Arg
                     (list Versors ...)))))

    (define-syntax negate
      (syntax-rules ()
        ((negate V)
         (%create-val "geomalg.negate"
                     (syntax-source-loc V)
                     V))))

    (define-syntax rev
      (syntax-rules ()
        ((rev V)
         (%create-val "geomalg.rev"
                      (syntax-source-loc V)
                      V))))

    (define-syntax grade-invo
      (syntax-rules ()
        ((grade-invo V)
         (create-val "geomalg.grade_invo"
                     (syntax-source-loc V)
                     V))))

    (define-syntax inverse
      (syntax-rules ()
        ((inverse V)
         (%create-val "geomalg.inverse"
                     (syntax-source-loc V)
                     V))))

    ; Shortcut to create basis blade constant.
    ; (For scalar multiplication use iprod.)
    (define (%create-blade-val OpName ResultType Loc Coeff)
      (result
        (create-op OpName
                   (loc: Loc)
                   (operands:)
                   (attributes:
                     ("coefficient"
                       (float-attr Coeff !f32)))
                   (result-types: ResultType)
                   )))

    ; TODO Local syntax would enable consolidating the boiler plate here
    ;      ... probably.
    ; Construct basis blades with constant coefficient.
    (define-syntax scalar
      (syntax-rules ()
        ((scalar Coeff)
         (%create-blade-val "geomalg.blade"
                            !scalar
                            (syntax-source-loc Coeff)
                            Coeff))))
    (define-syntax e1
      (syntax-rules ()
        ((e1 Coeff)
         (%create-blade-val "geomalg.blade"
                            !e1
                            (syntax-source-loc Coeff)
                            Coeff))))
    (define-syntax e2
      (syntax-rules ()
        ((e2 Coeff)
         (%create-blade-val "geomalg.blade"
                            !e2
                            (syntax-source-loc Coeff)
                            Coeff))))
    (define-syntax e3
      (syntax-rules ()
        ((e3 Coeff)
         (%create-blade-val "geomalg.blade"
                            !e3
                            (syntax-source-loc Coeff)
                            Coeff))))
    (define-syntax no
      (syntax-rules ()
        ((no Coeff)
         (%create-blade-val "geomalg.blade"
                            !no
                            (syntax-source-loc Coeff)
                            Coeff))))
    (define-syntax ni
      (syntax-rules ()
        ((ni Coeff)
         (%create-blade-val "geomalg.blade"
                            !ni
                            (syntax-source-loc Coeff)
                            Coeff))))

    (define-syntax convert
      (syntax-rules ()
        ((convert V Type)
         (result
           (create-op "geomalg.convert"
                      (loc: (syntax-source-loc V))
                      (operands: V)
                      (attributes:)
                      (result-types: Type))))))

    (define (expand-impl Loc V)
      (define Op
        (create-op "geomalg.expand"
                   (loc: Loc)
                   (operands: V)
                   (attributes:)))
      (result-values Op))

    (define-syntax expand
      (syntax-rules ()
        ((expand V)
         (expand-impl (syntax-source-loc V) V))
                    ))
    )) ;; define-library
