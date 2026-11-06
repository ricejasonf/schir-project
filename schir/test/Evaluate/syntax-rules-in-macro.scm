; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define x 'global-x)
(define helper 'global-helper)
(define-syntax my-begin (syntax-rules () ((_ e ...) (begin e ...))))

; Test a top level syntax-rules produced by a macro.
(my-begin (define-syntax one (syntax-rules () ((_) 1))))

; CHECK: 1
(write (one))
(newline)

; Test that a binding in the template does not capture the input.
(my-begin
  (define-syntax add5 (syntax-rules () ((_ e) (let ((tmp 5)) (+ tmp e)))))
  (define tmp 2)
  ; CHECK-NEXT: 7
  (write (add5 tmp))
  (newline))

; Test that a use site binding does not capture a free identifier.
(my-begin
  (define-syntax get-x (syntax-rules () ((_) x)))
  ; CHECK-NEXT: global-x
  (write (let ((x 2)) (get-x)))
  (newline))

; Test a local syntax in a let body.
(define (let-body)
  (let ((a 1))
    (define-syntax get-a (syntax-rules () ((_) a)))
    (get-a)))

; CHECK-NEXT: 1
(write (let-body))
(newline)

; Test that a template binding does not capture the input in a let body.
(define (swap-in-let)
  (let ((p 1) (tmp 2))
    (define-syntax swap!
      (syntax-rules ()
        ((swap! a b) (let ((tmp a)) (set! a b) (set! b tmp)))))
    (swap! p tmp)
    (list p tmp)))

; CHECK-NEXT: (2 1)
(write (swap-in-let))
(newline)

; Test that a nested use site binding does not capture a free identifier.
(define (nested-shadow)
  (let ((a 1))
    (define-syntax get-a (syntax-rules () ((_) a)))
    (let ((a 2)) (get-a))))

; CHECK-NEXT: 1
(write (nested-shadow))
(newline)

; Test that a free identifier in a let body defaults to a global.
(define (let-free-global)
  (let ()
    (define-syntax get-x (syntax-rules () ((_) x)))
    (let ((x 2)) (get-x))))

; CHECK-NEXT: global-x
(write (let-free-global))
(newline)

; Test that a free identifier in a let body refers to a later local.
(define (let-local-after)
  (let ()
    (define-syntax get-y (syntax-rules () ((_) y)))
    (define y 'local-y)
    (get-y)))

; CHECK-NEXT: local-y
(write (let-local-after))
(newline)

; Test a literal and a recursive local syntax in a let body.
(define (let-literal)
  (let ()
    (define-syntax my-cond
      (syntax-rules (else)
        ((_ (else e)) e)
        ((_ (c e) rest ...) (if c e (my-cond rest ...)))))
    (list (my-cond (#f 1) (else 2)) (my-cond (#t 3) (else 4)))))

; CHECK-NEXT: (2 3)
(write (let-literal))
(newline)

; Test identifiers of the same name closed in different environments.
(define-syntax def-getter
  (syntax-rules ()
    ((_ name e)
     (define-syntax name (syntax-rules () ((_) (list e helper)))))))

(define (same-name)
  (let ((helper 'local-helper))
    (def-getter get-helpers helper)
    (get-helpers)))

; CHECK-NEXT: (local-helper global-helper)
(write (same-name))
(newline)

; Test that a use site binding does not capture a macro's input.
(define (same-name-shadow)
  (let ((helper 'local-helper))
    (def-getter get-helpers helper)
    (let ((helper 'inner-helper))
      (get-helpers))))

; CHECK-NEXT: (local-helper global-helper)
(write (same-name-shadow))
(newline)

(def-getter get-1 1)
(def-getter get-2 2)

; CHECK-NEXT: ((1 global-helper) (2 global-helper))
(write (list (get-1) (get-2)))
(newline)

; Test a custom ellipsis closed in a macro template.
(define-syntax def-lister
  (syntax-rules ()
    ((_ name)
     (define-syntax name (syntax-rules ::: () ((_ e :::) (list e :::)))))))
(def-lister my-list)

; CHECK-NEXT: (1 2 3)
(write (my-list 1 2 3))
(newline)

; Test a transformer passed to a macro.
(define-syntax my-define-syntax
  (syntax-rules ()
    ((_ name transformer) (define-syntax name transformer))))
(my-define-syntax two (syntax-rules () ((_) 2)))

(define (local-transformer)
  (my-define-syntax three (syntax-rules () ((_) 3)))
  (three))

; CHECK-NEXT: (2 3)
(write (list (two) (local-transformer)))
(newline)

; Test a local syntax that uses a local syntax defined by a macro.
(define (macro-defined-keyword)
  (def-getter get-pair 'x)
  (define-syntax use-get-pair (syntax-rules () ((_) (get-pair))))
  (use-get-pair))

; CHECK-NEXT: (x global-helper)
(write (macro-defined-keyword))
(newline)

; Test a local macro that defines a local syntax with a template binding.
(define (local-macro-template-binding)
  (define-syntax def-add5
    (syntax-rules ()
      ((_ name)
       (define-syntax name
         (syntax-rules () ((_ e) (let ((tmp 5)) (+ tmp e))))))))
  (def-add5 add5)
  (define tmp 2)
  (add5 tmp))

; CHECK-NEXT: 7
(write (local-macro-template-binding))
(newline)

; Test a local macro that defines a local syntax with a macro input.
(define (local-macro-defining-macro)
  (define-syntax def-const
    (syntax-rules ()
      ((_ name v) (define-syntax name (syntax-rules () ((_) v))))))
  (let ((v 10))
    (def-const ten v)
    (let ((v 20)) (ten))))

; CHECK-NEXT: 10
(write (local-macro-defining-macro))
(newline)

; Test a local macro that defines a local syntax with a free local.
(define (local-macro-free-local)
  (define v 10)
  (define-syntax def-const
    (syntax-rules ()
      ((_ name) (define-syntax name (syntax-rules () ((_) v))))))
  (def-const ten)
  (let ((v 20))
    (ten)))

; CHECK-NEXT: 10
(write (local-macro-free-local))
(newline)

; Test a local syntax that refers to a binding of its macro.
(define count 'global-count)
(define-syntax with-counter
  (syntax-rules ()
    ((_ inc! body ...)
     (let ((count 0))
       (define-syntax inc! (syntax-rules () ((_) (set! count (+ count 1)))))
       body ...
       count))))

; CHECK-NEXT: (2 global-count)
(write (list (with-counter bump! (bump!) (bump!)) count))
(newline)

; Test that nested expansions of a local syntax bind distinct closed
; identifiers.
(define-syntax with-count
  (syntax-rules ()
    ((_ m body)
     (let ((count 0))
       (define-syntax m
         (syntax-rules ()
           ((_ e) (let ((count (+ count 1))) (list count e)))))
       body))))

; CHECK-NEXT: (1 (1 x))
(write (with-count m (m (m 'x))))
(newline)

; Test that a closed identifier refers to a local defined after the syntax.
(define (closed-local-after)
  (let ()
    (define-syntax define-get-y
      (syntax-rules ()
        ((_ name) (define (name) y))))
    (define-get-y get)
    (define y 'local-y)
    (get)))

; CHECK-NEXT: local-y
(write (closed-local-after))
(newline)
