(import (schir base))

;; Conformal constructors for 2D Euclidean space (the e₁ ∧ e₂ plane).
;;
;; Export (geomalg conformal) along with point and line built from dual
;; planes, so they keep the weight -∞·X of a point X.
;;
;; Use (with-metric 'cga) before expanding.
(define-library (geomalg conformal-2d)
  (export
    up
    down
    normalize-point
    point-weight
    plane
    bisector
    sphere
    point
    line
    translator
    rotor
    dilator
    transversor
    ;; Procedures taking an explicit source location
    up-aux
    down-aux
    normalize-point-aux
    point-weight-aux
    plane-aux
    bisector-aux
    sphere-aux
    point-aux
    line-aux
    translator-aux
    rotor-aux
    dilator-aux
    transversor-aux
    )
  (import (schir base)
          (schir mlir)
          (geomalg base)
          (geomalg versor)
          (geomalg conformal))
  (begin
    (define !f32 (type "f32"))

    ;; Create the blade of type T with the constant coefficient C.
    (define (%blade Loc T C)
      (result
        (create-op "geomalg.blade"
                   (loc: Loc)
                   (operands:)
                   (attributes: ("coefficient" (float-attr C !f32)))
                   (result-types: T))))

    ;; Dual plane through c normal to n.
    (define (%plane-through Loc n c)
      (plane-aux Loc n (iprod-aux Loc n c)))

    ;; Point P = (e₁ + c₁∞)(e₂ + c₂∞) as two dual planes through c.
    ;; As a versor: point reflection x ↦ 2c - x (a half-turn about c).
    (define (point-aux Loc c)
      (versor-aux Loc (%plane-through Loc (%blade Loc !e1 1) c)
                      (%plane-through Loc (%blade Loc !e2 1) c)))

    (define-syntax point
      (syntax-rules ()
        ((point c)
         (point-aux (syntax-source-loc) c))))

    ;; Line through c with direction u as the dual plane through c
    ;; normal to u⌋(e₁ ∧ e₂).
    ;; As a versor: reflection in the line.
    (define (line-aux Loc c u)
      (define I (oprod-aux Loc (%blade Loc !e1 1) (%blade Loc !e2 1)))
      (%plane-through Loc (iprod-aux Loc u I) c))

    (define-syntax line
      (syntax-rules ()
        ((line c u)
         (line-aux (syntax-source-loc) c u))))
    )) ;; define-library
