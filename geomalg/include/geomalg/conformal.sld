(import (schir base))

;; Subspaces and conformal maps of the conformal model (CGA) as versors.
;;
;;   o = no, ∞ = ni, o·∞ = -1, o² = ∞² = 0
;;   x is a Euclidean vector and X = o + x + ½x²∞ is its conformal point.
;;
;; Constructors return 1-vectors or factored versors for use with vprod.
;; Versors made of planes (e.g. translators and rotors) keep the weight
;; -∞·X of a point X. Others scale it (and flat points negate it since
;; F∞F⁻¹ = -∞), so use normalize-point or down on their images.
;;
;; Scalar arguments may be scalar values or numbers.
;; Use (with-metric 'cga) before expanding.
(define-library (geomalg conformal)
  (export
    up
    down
    normalize-point
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
          (geomalg versor))
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

    ;; Convert a number to a scalar value.
    (define (%scalar Loc S)
      (if (number? S) (%blade Loc !scalar S) S))

    ;; Multiply V by the scalar S.
    (define (%scale Loc S V)
      (iprod-aux Loc (%scalar Loc S) V))

    (define (%square Loc V)
      (iprod-aux Loc V V))

    ;; Dual sphere σ = X - ½ρ²∞ with center point X and squared radius ρ².
    (define (%sphere Loc X RadiusSq)
      (sum-aux Loc X (%scale Loc (%scale Loc -0.5 RadiusSq)
                         (%blade Loc !ni 1))))

    ;; Conformal point X = o + x + ½x²∞.
    ;; (A null vector which is not invertible.)
    (define (up-aux Loc x)
      (sum-aux Loc (%blade Loc !no 1)
                   x
                   (%scale Loc (%scale Loc .5 (%square Loc x))
                           (%blade Loc !ni 1))))

    (define-syntax up
      (syntax-rules ()
        ((up x)
         (up-aux (syntax-source-loc) x))))

    ;; Weight -∞·X of a point X.
    (define (%weight Loc X)
      (negate-aux Loc (iprod-aux Loc (%blade Loc !ni 1) X)))

    ;; Euclidean point x = (E ⌋ (X ∧ E)) / (-∞·X) where E = o ∧ ∞.
    (define (down-aux Loc X)
      (define E (oprod-aux Loc (%blade Loc !no 1) (%blade Loc !ni 1)))
      (iprod-aux Loc (inverse-aux Loc (%weight Loc X))
                     (iprod-aux Loc E (oprod-aux Loc X E))))

    (define-syntax down
      (syntax-rules ()
        ((down X)
         (down-aux (syntax-source-loc) X))))

    ;; Point X / (-∞·X) of weight 1 (i.e. o + x + ½x²∞).
    (define (normalize-point-aux Loc X)
      (iprod-aux Loc (inverse-aux Loc (%weight Loc X)) X))

    (define-syntax normalize-point
      (syntax-rules ()
        ((normalize-point X)
         (normalize-point-aux (syntax-source-loc) X))))

    ;; Dual plane π = n + δ∞ of the points where n·x = δ.
    ;; As a versor: reflection in the plane (a line in 2D).
    (define (plane-aux Loc n Dist)
      (sum-aux Loc n (%scale Loc Dist (%blade Loc !ni 1))))

    (define-syntax plane
      (syntax-rules ()
        ((plane n Dist)
         (plane-aux (syntax-source-loc) n Dist))))

    ;; Dual plane π = A - B = (a - b) + ½(a² - b²)∞ bisecting a and b.
    ;; As a versor: reflection that swaps a and b.
    (define (bisector-aux Loc a b)
      (sum-aux Loc a
                   (negate-aux Loc b)
                   (%scale Loc (%scale Loc .5 (sum-aux Loc (%square Loc a)
                                                (negate-aux Loc
                                                  (%square Loc b))))
                           (%blade Loc !ni 1))))

    (define-syntax bisector
      (syntax-rules ()
        ((bisector a b)
         (bisector-aux (syntax-source-loc) a b))))

    ;; Dual sphere σ = C - ½ρ²∞ with center c and radius ρ.
    ;; As a versor: inversion x ↦ c + ρ²(x - c)/|x - c|².
    (define (sphere-aux Loc c Radius)
      (define R (%scalar Loc Radius))
      (%sphere Loc (up-aux Loc c) (%square Loc R)))

    (define-syntax sphere
      (syntax-rules ()
        ((sphere c Radius)
         (sphere-aux (syntax-source-loc) c Radius))))

    ;; Flat point F = (C + ½∞)(C - ½∞) = -C ∧ ∞ at c.
    ;; As a versor: point reflection x ↦ 2c - x.
    (define (point-aux Loc c)
      (dilator-aux Loc c -1))

    (define-syntax point
      (syntax-rules ()
        ((point c)
         (point-aux (syntax-source-loc) c))))

    ;; Line Λ = π F = C ∧ u ∧ ∞ through c with direction u
    ;; where π is the dual plane through c normal to u.
    ;; As a versor: half-turn about the line in 3D
    ;; (reflection in the line in 2D).
    (define (line-aux Loc c u)
      (versor-aux Loc (plane-aux Loc u (iprod-aux Loc u c))
                      (point-aux Loc c)))

    (define-syntax line
      (syntax-rules ()
        ((line c u)
         (line-aux (syntax-source-loc) c u))))

    ;; Get t or e₁ if t = 0.
    (define (%direction Loc t)
      (define IsZero
        (result
          (create-op "geomalg.select_lt"
                     (loc: Loc)
                     (operands: (%square Loc t)
                                (%blade Loc !scalar 1.1754943508222875e-38)
                                (%blade Loc !scalar 1)
                                (%blade Loc !scalar 0))
                     (attributes:)
                     (result-types: !scalar))))
      (sum-aux Loc t (%scale Loc IsZero (%blade Loc !e1 1))))

    ;; Translator T = (a + ½(a·t)∞) a ∝ 1 - ½t∞ as two parallel dual planes
    ;; with a = t (or a = e₁ for t = 0)
    ;; or T = (n + ½d∞) n ∝ 1 - ½dn∞ for a unit normal n.
    ;; As a versor: translation x ↦ x + t or x ↦ x + dn.
    (define (translator-aux Loc t . MaybeDist)
      (if (null? MaybeDist)
        (let ((a (%direction Loc t)))
          (versor-aux Loc (plane-aux Loc a (%scale Loc .5 (iprod-aux Loc a t)))
                          a))
        (versor-aux Loc (plane-aux Loc t (%scale Loc .5 (car MaybeDist)))
                        t)))

    (define-syntax translator
      (syntax-rules ()
        ((translator t)
         (translator-aux (syntax-source-loc) t))
        ((translator n Dist)
         (translator-aux (syntax-source-loc) n Dist))))

    ;; Rotor R = b a as two dual planes through the origin
    ;; or R = (b + (b·c)∞)(a + (a·c)∞) as two dual planes through c.
    ;; As a versor: rotation in the a ∧ b plane by twice the angle
    ;; from a to b about the origin or c.
    (define (rotor-aux Loc a b . MaybeCenter)
      (if (null? MaybeCenter)
        (versor-aux Loc b a)
        (let ((c (car MaybeCenter)))
          (versor-aux Loc (plane-aux Loc b (iprod-aux Loc b c))
                          (plane-aux Loc a (iprod-aux Loc a c))))))

    (define-syntax rotor
      (syntax-rules ()
        ((rotor a b)
         (rotor-aux (syntax-source-loc) a b))
        ((rotor a b c)
         (rotor-aux (syntax-source-loc) a b c))))

    ;; Dilator D = (C - ½s∞)(C - ½∞) as two dual spheres centered at c
    ;; (D ∝ (1 + s) + (s - 1) o ∧ ∞ for c = 0).
    ;; As a versor: scaling x ↦ c + s(x - c) for s ≠ 0.
    (define (dilator-aux Loc c Scale)
      (define C (up-aux Loc c))
      (versor-aux Loc (%sphere Loc C (%scalar Loc Scale))
                      (%sphere Loc C (%scalar Loc 1))))

    (define-syntax dilator
      (syntax-rules ()
        ((dilator c Scale)
         (dilator-aux (syntax-source-loc) c Scale))))

    ;; Transversor K = σ T σ ∝ 1 + o b where σ = o - ½∞ is the unit sphere
    ;; and T is the translator for b (or dn as with translator).
    ;; As a versor: transversion x ↦ (x + x²b) / (1 + 2b·x + b²x²).
    (define (transversor-aux Loc . TranslatorArgs)
      (define Sigma (%sphere Loc (%blade Loc !no 1) (%scalar Loc 1)))
      (versor-aux Loc Sigma
                      (apply translator-aux Loc TranslatorArgs)
                      Sigma))

    (define-syntax transversor
      (syntax-rules ()
        ((transversor b)
         (transversor-aux (syntax-source-loc) b))
        ((transversor n Dist)
         (transversor-aux (syntax-source-loc) n Dist))))
    )) ;; define-library
