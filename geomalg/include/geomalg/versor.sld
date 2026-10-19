(import (schir base))

;; Versors are geometric products of 1-vectors that are kept factored
;; so vprod can apply them one factor at a time.
(define-library (geomalg versor)
  (export
    !versor
    versor
    ;; Procedures taking an explicit source location
    versor-aux
    )
  (import (except (schir base) + - * /)
          (schir mlir)
          (geomalg base)
          (geomalg infix))
  (begin
    ;; Take 1-vector and versor types flattening the factors of versors.
    (define !versor
      (load-builtin "geomalg_versor_type"))

    (define (versor-aux Loc . Vs)
      (define Op
        (create-op "geomalg.versor"
                   (loc: Loc)
                   (operands: Vs)
                   (attributes:)))
      (verify Op)
      (result Op))

    ;; Construct a versor as the geometric product of 1-vectors and versors.
    (define-syntax versor
      (syntax-rules ()
        ((versor V1 VN ...)
         (versor-aux (syntax-source-loc) V1 VN ...))))
    )) ;; define-library
