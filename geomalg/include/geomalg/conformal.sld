(import (schir base))

;; Subspaces and conformal maps of the conformal model (CGA) as versors.
;;
;;   o = no, ∞ = ni, o·∞ = -1, o² = ∞² = 0
;;   x is a Euclidean vector and X = o + x + ½x²∞ is its conformal point.
;;
;; Constructors return 1-vectors or factored versors for use with vprod.
;; Versors made of planes (e.g. translators and rotors) keep the weight
;; -∞·X of a point X. Others scale it (and dilators with s < 0 negate it),
;; so use normalize-point or down on their images.
;; Import (geomalg conformal-2d) or (geomalg conformal-3d) for these
;; constructors along with point, line and negative-dilator.
;;
;; Scalar arguments may be scalar values or numbers.
;; Use (with-metric 'cga) before expanding.
(define-library (geomalg conformal)
  (export
    up
    down
    normalize-point
    point-weight
    plane
    bisector
    sphere
    translator
    rotor
    dilator
    transversor
    ;; Procedures taking an explicit source location
    up*
    down*
    normalize-point*
    point-weight*
    plane*
    bisector*
    sphere*
    translator*
    rotor*
    dilator*
    transversor*
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
      (iprod* Loc (%scalar Loc S) V))

    (define (%square Loc V)
      (iprod* Loc V V))

    ;; Dual sphere σ = X - ½ρ²∞ with center point X and squared radius ρ².
    (define (%sphere Loc X RadiusSq)
      (sum* Loc X (%scale Loc (%scale Loc -0.5 RadiusSq)
                         (%blade Loc !ni 1))))

    ;; Conformal point X = o + x + ½x²∞.
    ;; (A null vector which is not invertible.)
    (define (up* Loc x)
      (sum* Loc (%blade Loc !no 1)
                   x
                   (%scale Loc (%scale Loc .5 (%square Loc x))
                           (%blade Loc !ni 1))))

    (define-syntax up
      (syntax-rules ()
        ((up x)
         (up* (syntax-source-loc) x))))

    ;; Weight -∞·X of a point X.
    (define (point-weight* Loc X)
      (negate* Loc (iprod* Loc (%blade Loc !ni 1) X)))

    (define-syntax point-weight
      (syntax-rules ()
        ((point-weight X)
         (point-weight* (syntax-source-loc) X))))

    ;; Euclidean point x = (E ⌋ (X ∧ E)) / (-∞·X) where E = o ∧ ∞.
    (define (down* Loc X)
      (define E (oprod* Loc (%blade Loc !no 1) (%blade Loc !ni 1)))
      (iprod* Loc (inverse* Loc (point-weight* Loc X))
                     (iprod* Loc E (oprod* Loc X E))))

    (define-syntax down
      (syntax-rules ()
        ((down X)
         (down* (syntax-source-loc) X))))

    ;; Point X / (-∞·X) of weight 1 (i.e. o + x + ½x²∞).
    (define (normalize-point* Loc X)
      (iprod* Loc (inverse* Loc (point-weight* Loc X)) X))

    (define-syntax normalize-point
      (syntax-rules ()
        ((normalize-point X)
         (normalize-point* (syntax-source-loc) X))))

    ;; Dual plane π = n + δ∞ of the points where n·x = δ.
    ;; As a versor: reflection in the plane (a line in 2D).
    (define (plane* Loc n Dist)
      (sum* Loc n (%scale Loc Dist (%blade Loc !ni 1))))

    (define-syntax plane
      (syntax-rules ()
        ((plane n Dist)
         (plane* (syntax-source-loc) n Dist))))

    ;; Dual plane π = A - B = (a - b) + ½(a - b)·(a + b)∞ bisecting a and b.
    ;; As a versor: reflection that swaps a and b.
    (define (bisector* Loc a b)
      (define n (sum* Loc a (negate* Loc b)))
      (plane* Loc n (%scale Loc .5 (iprod* Loc n (sum* Loc a b)))))

    (define-syntax bisector
      (syntax-rules ()
        ((bisector a b)
         (bisector* (syntax-source-loc) a b))))

    ;; Dual sphere σ = C - ½ρ²∞ with center c and radius ρ.
    ;; As a versor: inversion x ↦ c + ρ²(x - c)/|x - c|².
    (define (sphere* Loc c Radius)
      (define R (%scalar Loc Radius))
      (%sphere Loc (up* Loc c) (%square Loc R)))

    (define-syntax sphere
      (syntax-rules ()
        ((sphere c Radius)
         (sphere* (syntax-source-loc) c Radius))))

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
      (sum* Loc t (%scale Loc IsZero (%blade Loc !e1 1))))

    ;; Translator T = (a + ½(a·t)∞) a ∝ 1 - ½t∞ as two parallel dual planes
    ;; with a = t (or a = e₁ for t = 0)
    ;; or T = (n + ½d∞) n ∝ 1 - ½dn∞ for a unit normal n.
    ;; As a versor: translation x ↦ x + t or x ↦ x + dn.
    (define (translator* Loc t . MaybeDist)
      (if (null? MaybeDist)
        (let ((a (%direction Loc t)))
          (versor* Loc (plane* Loc a (%scale Loc .5 (iprod* Loc a t)))
                          a))
        (versor* Loc (plane* Loc t (%scale Loc .5 (car MaybeDist)))
                        t)))

    (define-syntax translator
      (syntax-rules ()
        ((translator t)
         (translator* (syntax-source-loc) t))
        ((translator n Dist)
         (translator* (syntax-source-loc) n Dist))))

    ;; Rotor R = b a as two dual planes through the origin
    ;; or R = (b + (b·c)∞)(a + (a·c)∞) as two dual planes through c.
    ;; As a versor: rotation in the a ∧ b plane by twice the angle
    ;; from a to b about the origin or c.
    (define (rotor* Loc a b . MaybeCenter)
      (if (null? MaybeCenter)
        (versor* Loc b a)
        (let ((c (car MaybeCenter)))
          (versor* Loc (plane* Loc b (iprod* Loc b c))
                          (plane* Loc a (iprod* Loc a c))))))

    (define-syntax rotor
      (syntax-rules ()
        ((rotor a b)
         (rotor* (syntax-source-loc) a b))
        ((rotor a b c)
         (rotor* (syntax-source-loc) a b c))))

    ;; Dilator D = (C - ½s∞)(C - ½∞) as two dual spheres centered at c
    ;; (D ∝ (1 + s) + (s - 1) o ∧ ∞ for c = 0).
    ;; As a versor: scaling x ↦ c + s(x - c) for s > 0
    ;; (s < 0 negates the weight of points, so use negative-dilator).
    (define (dilator* Loc c Scale)
      (define C (up* Loc c))
      (versor* Loc (%sphere Loc C (%scalar Loc Scale))
                      (%sphere Loc C (%scalar Loc 1))))

    (define-syntax dilator
      (syntax-rules ()
        ((dilator c Scale)
         (dilator* (syntax-source-loc) c Scale))))

    ;; Transversor K = σ T σ ∝ 1 + o b where σ = o - ½∞ is the unit sphere
    ;; and T is the translator for b (or dn as with translator).
    ;; As a versor: transversion x ↦ (x + x²b) / (1 + 2b·x + b²x²).
    (define (transversor* Loc . TranslatorArgs)
      (define Sigma (%sphere Loc (%blade Loc !no 1) (%scalar Loc 1)))
      (versor* Loc Sigma
                   (apply translator* Loc TranslatorArgs)
                   Sigma))

    (define-syntax transversor
      (syntax-rules ()
        ((transversor b)
         (transversor* (syntax-source-loc) b))
        ((transversor n Dist)
         (transversor* (syntax-source-loc) n Dist))))
    )) ;; define-library
