; RUN: schir-scheme --mode=repl-check --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

; CHECK: (1 2 "yo")
(list 1 2 "yo")

(define nums (list 1 2 3))
; CHECK: (1 2 3)
nums
; CHECK-NEXT: (0 1 2 3)
(cons 0 nums)
; CHECK-NEXT: (1 2 3)
nums
; CHECK-NEXT: 1
(car nums)
; CHECK-NEXT: (2 3)
(cdr nums)
; CHECK-NEXT: (1 2 3)
(append nums)
; CHECK-NEXT: (1 2 3)
(append nums '())
; CHECK-NEXT: (1 2 3)
(append '() nums)
; CHECK-NEXT: (4 5 6 1 2 3)
(append '(4 5 6) nums)
; CHECK-NEXT: (1 2 3 4 5 6)
(append nums '(4 5 6))
; CHECK-NEXT: (0 1 2 3 4 5 6)
(append '(0) nums '(4 5 6))
; CHECK-NEXT: (0 1 2 3 4 5 6 1 2 3)
(append '(0) nums '(4 5 6) nums)
; CHECK-NEXT: (1 2 3)
nums

;; List Formals
; CHECK-NEXT: #t
((lambda args
   (null? args)))

; CHECK-NEXT: (1 2 4 5 6)
((lambda args
   args)
 1 2 4 5 6)

; CHECK-NEXT: #(1 ())
((lambda (arg . args)
   #(arg args))
 1)

; CHECK-NEXT: #(1 2 ())
((lambda (arg1 arg2 . args)
   #(arg1 arg2 ()))
 1 2)

; CHECK-NEXT: #(1 (2))
((lambda (arg . args)
   #(arg args))
 1 2)

; CHECK-NEXT: (foo bar 4 5 6)
((lambda (foo . bar)
   (append foo bar))
 '(foo bar) 4 5 6)

; CHECK-NEXT: (foo bar 4 5 6)
((lambda (foo . bar)
   (append foo bar))
 '(foo bar) 4 5 6)

; CHECK-NEXT: (foo bar 4 5 6)
((lambda (foo bar . baz)
   (append foo (list bar) baz))
 '(foo bar) 4 5 6)

; CHECK-NEXT: (0 1 2 3 4)
(list (length '())
      (length '(9))
      (length '(9 42))
      (length '(9 (foo bar) 42))
      (length '((foo bar) 42 9 moo)))

; CHECK-NEXT: ()
(make-list 0)

; CHECK-NEXT: (0)
(make-list 1 0)

; CHECK-NEXT: (0 0 0 0 0)
(make-list 5 0)

; CHECK-NEXT: (foo bar baz)
(let ((List (make-list 3)))
  (list-set! List 0 'foo)
  (list-set! List 1 'bar)
  (list-set! List 2 'baz)
  List
  )

; CHECK-NEXT: (foo bar 0)
(let ((List (make-list 3 0)))
  (list-set! List 0 'foo)
  (list-set! List 1 'bar)
  List
  )

; CHECK-NEXT: (baz bar foo)
(let ((List (list 'foo 'bar 'baz)))
  (list (list-ref List 2)
        (list-ref List 1)
        (list-ref List 0)))

; CHECK-NEXT: ()
(reverse '())

; CHECK-NEXT: (1)
(reverse '(1))

; CHECK-NEXT: (5 4 3 2 1)
(reverse '(1 2 3 4 5))

; CHECK-NEXT: (baz uhh)
(cadr '((foo bar) (baz uhh)))

; CHECK-NEXT: (#t #f #t #f)
(map <
  '(0 1 1 3)
  '(1 2 2 7)
  '(2 0 3 3))

; CHECK-NEXT: ()
(map + '() '() '())

; CHECK-NEXT: (5)
(map + '(5))

; CHECK-NEXT: (7)
(map + '(5) '(2))

; CHECK-NEXT: (#(a b c) #(d e f) #(g h i))
(map vector
  '(a d g 'oof)
  '(b e h)
  '(c f i))

; CHECK-NEXT: (#(a b c) #(d e f) #(g h i))
(map vector
  '(a d g)
  '(b e h 'oof)
  '(c f i))

; CHECK-NEXT: (#(a b c) #(d e f) #(g h i))
(map vector
  '(a d g)
  '(b e h)
  '(c f i 'oof))

; CHECK-NEXT: (#((quote ())))
(map vector
  '('()))

; CHECK-NEXT: #t
(and
  (every number? '())
  (every number? '(1))
  (every number? '(1 2 3 4)))

; CHECK-NEXT: #f
(or
  (every number? '(foo))
  (every number? '(1 foo))
  (every number? '(1 2 3 #t 4)))

; CHECK-NEXT: #f
(or
  (any number? '())
  (any number? '(foo))
  (any number? '(foo #t bar baz)))

; CHECK-NEXT: #t
(and
  (any number? '(1))
  (any number? '(foo 1))
  (any number? '(1 foo))
  (any number? '(1 2 3 #t 4)))

; CHECK-NEXT: ((a b c) (b c) #f #f ((a) c) ("b" "c")
; CHECK-SAME:  (101 102) (101 102))
(list
  (memq 'a '(a b c))
  (memq 'b '(a b c))
  (memq 'a '(b c d))
  (memq (list 'a) '(b (a) c))
  (member (list 'a)
          '(b (a) c))
  (member "b"
          '(5 "b" "c"))
  (memq 101 '(100 101 102))
  (memv 101 '(100 101 102))
)

; CHECK: ((b 2) (a 1) (c 3) #f #f ((a)) (2 4) (5 7))
(let ()
  (define e '((a 1) (b 2) (c 3)))
  (list
    (assq 'b e)
    (assq 'a e)
    (assq 'c e)
    (assq 'd e)
    (assq (list 'a) '(((a)) ((b)) ((c))))
    (assoc (list 'a) '(((a)) ((b)) ((c))))
    (assoc 2.0 '((1 1) (2 4) (3 9)) >=)
    (assv 5 '((2 3) (5 7) (11 13)))
    ))

; CHECK-NEXT: (1 2 3 4 (5) (6) (7) (8))
(list (caaar '(((1))))
      (caadr '(0 (2)))
      (cadar '((0 3)))
      (caddr '(0 0 4))
      (cdaar '(((0 5))))
      (cdadr '(0 (0 6)))
      (cddar '((0 0 7)))
      (cdddr '(0 0 0 8)))
