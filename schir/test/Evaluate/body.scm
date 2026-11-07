; RUN: cd %S && schir-scheme --module-path=%schir_module_path \
; RUN:   --module-path=Inputs body.scm | FileCheck %s
(import (schir base))

;; A define follows an expression.
; CHECK: start2
(define (after-expr)
  (write 'start)
  (define x 2)
  x)
(write (after-expr))
(newline)

;; A macro expands to a define followed by an expression.
(define-syntax define-logged
  (syntax-rules ()
    ((_ name value)
     (begin
       (define name value)
       (write 'name)))))
; CHECK-NEXT: ab(1 2)
(define (logged)
  (define-logged a 1)
  (define-logged b (+ a 1))
  (list a b))
(write (logged))
(newline)

;; A procedure sees a define that follows an expression.
(define x 'outer)
; CHECK-NEXT: startinner
(define (forward)
  (define (get) x)
  (write 'start)
  (define x 'inner)
  (get))
(write (forward))
(newline)

;; A later define does not capture a free identifier of a template.
(define-syntax get-x
  (syntax-rules ()
    ((_) x)))
; CHECK-NEXT: (outer outer)
(define (template-free)
  (define a (get-x))
  (define x 'inner)
  (list a (get-x)))
(write (template-free))
(newline)

;; A local syntax follows an expression.
; CHECK-NEXT: 3
(define (local-syntax)
  (if #f #f)
  (define-syntax three
    (syntax-rules ()
      ((_) 3)))
  (three))
(write (local-syntax))
(newline)

;; A nested body owns its defines.
; CHECK-NEXT: (1 2)
(define (nested)
  (define p 1)
  (define q
    (let ()
      (if #f #f)
      (define p 2)
      p))
  (list p q))
(write (nested))
(newline)

;; An included file is spliced into the body.
; CHECK-NEXT: 7
(define (included)
  (if #f #f)
  (include "body-include.scm")
  included-value)
(write (included))
(newline)
