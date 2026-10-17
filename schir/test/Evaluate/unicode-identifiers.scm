; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define (√ x) (* x 2))
(define (∑ . xs) (apply + xs))
(define (café n) (+ n 1))
(define (λ-square x) (* x x))
(define (日本語 x) x)
(define (🙂 x) (list 'smile x))
(define π 3)
(define |a b| 4)

; CHECK: 42
(write (√ 21))
(newline)
; CHECK: 10
(write (∑ 1 2 3 4))
(newline)
; CHECK: 42
(write (café 41))
(newline)
; CHECK: 9
(write (λ-square 3))
(newline)
; CHECK: "konnichiwa"
(write (日本語 "konnichiwa"))
(newline)
; CHECK: (smile 1)
(write (🙂 1))
(newline)
; CHECK: 3
(write π)
(newline)
; CHECK: 4
(write |a b|)
(newline)

(define-syntax ⇒
  (syntax-rules ()
    ((⇒ α β) (list β α))))
; CHECK: (2 1)
(write (⇒ 1 2))
(newline)

(define-syntax for-all
  (syntax-rules (∈)
    ((_ x ∈ xs body) (map (lambda (x) body) xs))))
; CHECK: (10 20 30)
(write (for-all ξ ∈ '(1 2 3) (* ξ 10)))
(newline)
