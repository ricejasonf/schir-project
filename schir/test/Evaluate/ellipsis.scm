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
