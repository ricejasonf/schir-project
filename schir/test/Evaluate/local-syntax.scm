; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define y 'global-y)

; Test that a template binding does not capture the input.
(define (swap-test)
  (define-syntax swap!
    (syntax-rules ()
      ((swap! a b) (let ((tmp a)) (set! a b) (set! b tmp)))))
  (define p 1)
  (define tmp 2)
  (swap! p tmp)
  (list p tmp))

; CHECK: (2 1)
(write (swap-test))
(newline)

; Test that a free identifier refers to a local defined before the syntax.
(define (local-before)
  (define y 'local-y)
  (define-syntax get-y (syntax-rules () ((_) y)))
  (get-y))

; CHECK-NEXT: local-y
(write (local-before))
(newline)

; Test that a free identifier refers to a local defined after the syntax.
(define (local-after)
  (define-syntax get-y (syntax-rules () ((_) y)))
  (define y 'local-y)
  (get-y))

; CHECK-NEXT: local-y
(write (local-after))
(newline)

; Test that a free identifier refers to a local defined by a macro.
(define-syntax define-local
  (syntax-rules ()
    ((_ name v) (define name v))))
(define (macro-defined-local)
  (define-syntax get-y (syntax-rules () ((_) y)))
  (define-local y 'macro-local-y)
  (get-y))

; CHECK-NEXT: macro-local-y
(write (macro-defined-local))
(newline)

; Test that a define from a local syntax refers to a later local.
(define (expanded-define-local-after)
  (define-syntax define-get-y
    (syntax-rules ()
      ((_ name) (define (name) y))))
  (define-get-y get)
  (define y 'local-y)
  (get))

; CHECK-NEXT: local-y
(write (expanded-define-local-after))
(newline)

; Test that a use site binding does not capture a free identifier.
(define (use-site-shadow)
  (define y 'local-y)
  (define-syntax get-y (syntax-rules () ((_) y)))
  (let ((y 'inner-y))
    (get-y)))

; CHECK-NEXT: local-y
(write (use-site-shadow))
(newline)

; Test that a local in a nested lambda does not capture a free identifier.
(define (nested-lambda-shadow)
  (define y 'local-y)
  (define-syntax get-y (syntax-rules () ((_) y)))
  ((lambda ()
     (define y 'inner-y)
     (get-y))))

; CHECK-NEXT: local-y
(write (nested-lambda-shadow))
(newline)

; Test that a free identifier refers to a local in an enclosing frame.
(define (outer-frame x)
  (let ()
    (define-syntax get-x (syntax-rules () ((_) x)))
    (let ((x 'inner-x))
      (get-x))))

; CHECK-NEXT: outer-x
(write (outer-frame 'outer-x))
(newline)

; Test that a free identifier defaults to a global.
(define (free-global)
  (define-syntax get-y (syntax-rules () ((_) y)))
  (get-y))

; CHECK-NEXT: global-y
(write (free-global))
(newline)

; Test that a free identifier refers to a global defined later.
(define (free-later-global)
  (define-syntax get-later (syntax-rules () ((_) (later-global))))
  (get-later))
(define (later-global) 'later-global)

; CHECK-NEXT: later-global
(write (free-later-global))
(newline)

; Test a recursive local syntax.
(define (recursive)
  (define-syntax my-or
    (syntax-rules ()
      ((_) #f)
      ((_ e) e)
      ((_ e r ...) (let ((t e)) (if t t (my-or r ...))))))
  (define t 5)
  (my-or #f t))

; CHECK-NEXT: 5
(write (recursive))
(newline)

; Test that a free identifier is captured by a nested lambda.
(define (nested-lambda)
  (define x 10)
  (define-syntax get-x (syntax-rules () ((_) x)))
  ((lambda () (get-x))))

; CHECK-NEXT: 10
(write (nested-lambda))
(newline)

; Test a local syntax that expands to a define.
(define (introduce-define)
  (define-syntax def (syntax-rules () ((_ name v) (define name v))))
  (def z 42)
  z)

; CHECK-NEXT: 42
(write (introduce-define))
(newline)

; Test a local syntax that uses another local syntax.
(define (other-local-syntax)
  (define-syntax one (syntax-rules () ((_) 1)))
  (define-syntax plus-one (syntax-rules () ((_ e) (+ e (one)))))
  (plus-one 41))

; CHECK-NEXT: 42
(write (other-local-syntax))
(newline)

; Test a template binding of one local syntax around the use of another.
(define (other-local-syntax-binding)
  (define tmp 'local-tmp)
  (define-syntax get-tmp (syntax-rules () ((_) tmp)))
  (define-syntax with-tmp
    (syntax-rules ()
      ((_ e) (let ((tmp 'with-tmp)) e))))
  (with-tmp (get-tmp)))

; CHECK-NEXT: local-tmp
(write (other-local-syntax-binding))
(newline)

; Test that nested expansions of a local syntax bind distinct identifiers.
(define (nested-template-bindings)
  (define-syntax my-or
    (syntax-rules ()
      ((_) #f)
      ((_ e) e)
      ((_ e r ...) (let ((t e)) (if t t (my-or r ...))))))
  (define t 5)
  (list (my-or #f #f t) (let ((t 7)) (my-or #f t))))

; CHECK-NEXT: (5 7)
(write (nested-template-bindings))
(newline)

; Test that a use site binding does not capture a free global identifier.
(define (use-site-shadows-global)
  (define-syntax twice (syntax-rules () ((_ x) (list x x))))
  (let ((list vector)) (twice 1)))

; CHECK-NEXT: (1 1)
(write (use-site-shadows-global))
(newline)

; Test that a local syntax shadows a global syntax.
(define-syntax get-val (syntax-rules () ((_) 'global-syntax)))
(define (shadow-global-syntax)
  (define-syntax get-val (syntax-rules () ((_) 'local-syntax)))
  (get-val))

; CHECK-NEXT: local-syntax
(write (shadow-global-syntax))
(newline)

; CHECK-NEXT: global-syntax
(write (get-val))
(newline)

; Test a local syntax in a library.
(define-library (local-syntax-lib)
  (import (schir base))
  (export lib-fn)
  (begin
    (define (lib-fn)
      (define-syntax inc (syntax-rules () ((_ x) (+ x 1))))
      (inc 1))))
(import (local-syntax-lib))

; CHECK-NEXT: 2
(write (lib-fn))
(newline)
