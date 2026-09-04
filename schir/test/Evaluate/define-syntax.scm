; RUN: schir-scheme %s | FileCheck %s
(import (schir builtins))

;; TODO Local let-syntax needs to be tested/supported.

(define-syntax capture-late-bind
  (syntax-rules ()
    ((capture-late-bind x)
     ((lambda ()
       (define storage 0)
       (lambda ()
         (set! storage
           (if (symbol? ok)
               ok 'oknotyet))
         (write storage)))))))

; CHECK: oknotyet
((capture-late-bind 5))
(newline)

(define ok 'ok!)

; CHECK-NEXT: ok!
((capture-late-bind 5))

(newline)
(define-syntax my-lambda
  (syntax-rules (=>)
    ((my-lambda formals => body)
     (lambda formals
        body
        (write ok)
        (write "lambda args#:")
        (write (length 'formals))))))

; CHECK: (42 oops!)ok!"lambda args#:"1
((lambda (ok)
  ((my-lambda (x) => (write (list x ok))) 42))
 'oops!)
(newline)

; CHECK: 42"x has type Int""y has type Int"
; CHECK-NEXT: 5
; CHECK-NEXT: 6
(define-syntax my-lambda
  (syntax-rules (:)
    ((my-lambda ((arg : type) ...) body ...)
     (lambda (arg ...)
       (write 42)
       (write (string-append 'arg " has type " 'type)) ...
       (newline)
       body ...))))
((my-lambda ((x : Int) (y : Int))
  (write x)
  (newline)
  (write y)) 5 6)
(newline)

; CHECK: (0 1)
; CHECK-NEXT: (0 1 2)
(define-syntax ez
  (syntax-rules ()
    ((ez 0 1 i ...)
     '(0 1 i ...))))
(write (ez 0 1))
(newline)
(write (ez 0 1 2))
(newline)

; CHECK: (0 1 2 3 4 9)
(define-syntax ez
  (syntax-rules ()
    ((ez 0 1 i ... 5 6)
     '(0 1 i ... 9))))
(write (ez 0 1 2 3 4 5 6))
(newline)

(define-syntax my-define
  (syntax-rules ()
    ((my-define name x)
     (define name '(my name x)))))

; CHECK: (my my-tl 42)
(my-define my-tl 42)
(write my-tl)
(newline)

; CHECK: 12
((lambda ()
  (my-define not-my-local 12)
  (write not-my-local)
  (newline)))

; Test nested syntax closures.
(define-library (my my-let)
  (import (schir builtins))
  (begin
    (define-syntax my-let
      (syntax-rules ()
        ((my-let ((name val) ...) body1 body ...)
          ((lambda (name ...) body1 body ...) val ...))))
    )
  (export my-let)
  )

(import (my my-let))

; CHECK: 805
(my-let ((X 805))
  (my-let ((Y 806))
    (write X)))
(newline)

; Test lazy syntax name lookup.
(define-syntax %fwd-ref-a
  (syntax-rules ()
    ((%fwd-ref-a x) (%fwd-ref-b x))))
(define-syntax %fwd-ref-b
  (syntax-rules ()
    ((%fwd-ref-b x) (list 'b x))))

; CHECK: (b 99)
(write (%fwd-ref-a 99))
(newline)

; Test global variable versus syntax shadowing (both ways.)
(define shadow-var-then-syntax 111)

; CHECK: 111
(write shadow-var-then-syntax)
(newline)

(define-syntax shadow-var-then-syntax
  (syntax-rules ()
    ((shadow-var-then-syntax) 'now-syntax)))

; CHECK: now-syntax
(write (shadow-var-then-syntax))
(newline)

(define-syntax shadow-syntax-then-var
  (syntax-rules ()
    ((shadow-syntax-then-var) 'still-syntax)))

; CHECK: still-syntax
(write (shadow-syntax-then-var))
(newline)

(define shadow-syntax-then-var 222)

; CHECK: 222
(write shadow-syntax-then-var)
(newline)

; Test shadowing in imported library.
(define-library (shadow-test)
  (import (schir builtins))
  (begin
    (define (shadow-var-then-syntax) 1110)

    ; CHECK: 1110
    (write (shadow-var-then-syntax))
    (newline)

    (define-syntax shadow-var-then-syntax
      (syntax-rules ()
        ((shadow-var-then-syntax) 'now-syntax-lib)))

    ; CHECK: now-syntax-lib
    (write (shadow-var-then-syntax))
    (newline)

    (define-syntax shadow-syntax-then-var
      (syntax-rules ()
        ((shadow-syntax-then-var) 'still-syntax-lib)))

    ; CHECK: still-syntax-lib
    (write (shadow-syntax-then-var))
    (newline)

    (define (shadow-syntax-then-var) 2220)

    ; CHECK: 2220
    (write (shadow-syntax-then-var))
    (newline)))
(import (prefix (shadow-test) shadow-test-))

; Test free identifier's hygiene in lib.
(define-library (rename-global-hygiene-test)
  (import (schir builtins))
  (begin
    (define (shadow-helper) 'old-helper-fn)

    (define-syntax use-shadow-helper
      (syntax-rules ()
        ((use-shadow-helper) (shadow-helper))))

    (define-syntax shadow-helper
      (syntax-rules ()
        ((shadow-helper) 'shadowing-syntax)))

    ; CHECK: old-helper-fn
    (write (use-shadow-helper))
    (newline)

    ; CHECK-NEXT: shadowing-syntax
    (write (shadow-helper))
    (newline)))
(import (rename-global-hygiene-test))
