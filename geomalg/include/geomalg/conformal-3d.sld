(import (schir base))

;; Conformal constructors for 3D Euclidean space.
;;
;; Export (geomalg conformal) along with point and line built from dual
;; planes, so they keep the weight -∞·X of a point X, and negative-dilator
;; for scaling by s < 0.
;;
;; Use (with-metric 'cga) before expanding.
(define-library (geomalg conformal-3d)
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
    negative-dilator-aux
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

    ;; Point P = (e₁ + c₁∞)(e₂ + c₂∞)(e₃ + c₃∞) as three dual planes
    ;; through c.
    ;; As a versor: point reflection x ↦ 2c - x.
    (define (point-aux Loc c)
      (versor-aux Loc (%plane-through Loc (%blade Loc !e1 1) c)
                      (%plane-through Loc (%blade Loc !e2 1) c)
                      (%plane-through Loc (%blade Loc !e3 1) c)))

    (define-syntax point
      (syntax-rules ()
        ((point c)
         (point-aux (syntax-source-loc) c))))

    ;; Line Λ = π P through c with direction u where π is the dual plane
    ;; through c normal to u and P is the point at c.
    ;; As a versor: half-turn about the line.
    (define (line-aux Loc c u)
      (versor-aux Loc (%plane-through Loc u c)
                      (point-aux Loc c)))

    (define-syntax line
      (syntax-rules ()
        ((line c u)
         (line-aux (syntax-source-loc) c u))))

    ;; Negative dilator P D as the point P at c and the dilator D for -s,
    ;; which is odd (an improper map) and keeps the sign of the weight -∞·X
    ;; of a point X.
    ;; As a versor: scaling x ↦ c + s(x - c) for s < 0.
    (define (negative-dilator-aux Loc c Scale)
      (versor-aux Loc (point-aux Loc c)
                      (dilator-aux Loc c (if (number? Scale)
                                           (- Scale)
                                           (negate-aux Loc Scale)))))

    (define-syntax negative-dilator
      (syntax-rules ()
        ((negative-dilator c Scale)
         (negative-dilator-aux (syntax-source-loc) c Scale))))
    )) ;; define-library
