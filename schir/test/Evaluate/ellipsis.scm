; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir builtins))

(define-syntax trailing-item
  (syntax-rules ()
    ((trailing-item Names ... X)
     '(Names ... X))))

; CHECK: (a b c tail)
(write (trailing-item a b c tail))
(newline)

(define-syntax trailing-compound
  (syntax-rules ()
    ((trailing-compound (Stores ... Fn))
     '((Stores : store) ... (Fn : store)))))

; CHECK-NEXT: ((a : store) (b : store) (c : store))
(write (trailing-compound (a b c)))
(newline)

(define-syntax trailing-empty-pack
  (syntax-rules ()
    ((trailing-empty-pack Names ... X)
     '(Names ... X))))

; CHECK-NEXT: (only-one)
(write (trailing-empty-pack only-one))
(newline)

(define-syntax two-packs
  (syntax-rules ()
    ((two-packs (A ...) (B ...))
     '(A ... B ...))))

; CHECK-NEXT: (1 2 3 4 5)
(write (two-packs (1 2) (3 4 5)))
(newline)

(define-syntax nested-trailing
  (syntax-rules ()
    ((nested-trailing ((A ...) ...) X)
     '((A ...) ... X))))

; CHECK-NEXT: ((1 2) (3 4) last)
(write (nested-trailing ((1 2) (3 4)) last))
(newline)

(define-syntax front-append
  (syntax-rules ()
    ((front-append () (Acc ...)) '(Acc ...))
    ((front-append (X Rest ...) (Acc ...))
     (front-append (Rest ...) (Acc ... X)))))

; CHECK-NEXT: (1 2 3)
(write (front-append (1 2 3) ()))
(newline)

(define-syntax repeat-outer
  (syntax-rules ()
    ((repeat-outer x y ...) '((f x y) ...))))

; CHECK-NEXT: ((f a 1) (f a 2) (f a 3))
(write (repeat-outer a 1 2 3))
(newline)

; CHECK-NEXT: ()
(write (repeat-outer a))
(newline)

(define-syntax repeat-outer-nested
  (syntax-rules ()
    ((repeat-outer-nested x (y ...) ...) '((x (x y) ...) ...))))

; CHECK-NEXT: ((a (a 1) (a 2)) (a (a 3)))
(write (repeat-outer-nested a (1 2) (3)))
(newline)

(define-syntax repeat-pack-var
  (syntax-rules ()
    ((repeat-pack-var (z y) ...) '((z y y) ...))))

; CHECK-NEXT: ((1 2 2) (3 4 4))
(write (repeat-pack-var (1 2) (3 4)))
(newline)
