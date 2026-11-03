(import (schir base))

;; Conformal constructors for 2D Euclidean space (the e₁ ∧ e₂ plane).
;;
;; Export (geomalg conformal) along with point and line built from dual
;; planes, so they keep the weight -∞·X of a point X, and negative-dilator
;; for scaling by s < 0.
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
    negative-dilator
    transversor
    ;; Procedures taking an explicit source location
    up*
    down*
    normalize-point*
    point-weight*
    plane*
    bisector*
    sphere*
    point*
    line*
    translator*
    rotor*
    dilator*
    negative-dilator*
    transversor*
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
      (plane* Loc n (iprod* Loc n c)))

    ;; Point P = (e₁ + c₁∞)(e₂ + c₂∞) as two dual planes through c.
    ;; As a versor: point reflection x ↦ 2c - x (a half-turn about c).
    (define (point* Loc c)
      (versor* Loc (%plane-through Loc (%blade Loc !e1 1) c)
                      (%plane-through Loc (%blade Loc !e2 1) c)))

    (define-syntax point
      (syntax-rules ()
        ((point c)
         (point* (syntax-source-loc) c))))

    ;; Line through c with direction u as the dual plane through c
    ;; normal to u⌋(e₁ ∧ e₂).
    ;; As a versor: reflection in the line.
    (define (line* Loc c u)
      (define I (oprod* Loc (%blade Loc !e1 1) (%blade Loc !e2 1)))
      (%plane-through Loc (iprod* Loc u I) c))

    (define-syntax line
      (syntax-rules ()
        ((line c u)
         (line* (syntax-source-loc) c u))))

    ;; Negative dilator P D as the point P at c and the dilator D for -s,
    ;; which is even (a proper map) and keeps the sign of the weight -∞·X
    ;; of a point X.
    ;; As a versor: scaling x ↦ c + s(x - c) for s < 0.
    (define (negative-dilator* Loc c Scale)
      (versor* Loc (point* Loc c)
                      (dilator* Loc c (if (number? Scale)
                                           (- Scale)
                                           (negate* Loc Scale)))))

    (define-syntax negative-dilator
      (syntax-rules ()
        ((negative-dilator c Scale)
         (negative-dilator* (syntax-source-loc) c Scale))))
    )) ;; define-library
