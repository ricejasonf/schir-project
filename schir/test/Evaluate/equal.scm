; RUN: schir-scheme --module-path=%schir_module_path %s
(import (schir base))

(assert (eqv? #t #t))
(assert (eqv? #f #f))
(assert (eqv? 0 0))
(assert (eqv? 1 1))
(assert (eqv? 2 2))
(assert (eqv? 42 42))
(assert (eqv? '() '()))
(assert (eqv? equal? equal?))

(assert (equal? 0 0))
(assert (equal? 1 1))
(assert (equal? 2 2))
(assert (equal? 42 42))
(assert (equal? "" ""))
(assert (equal? "abc" "abc"))
(assert (equal? '() '()))
(assert (equal? (list 'a) '(a)))
(assert (equal? (list "abc") (list "abc")))
(assert (equal? (list "abc" 1) (list "abc" 1)))
(assert (equal? (list "abc" 1 (list 4 5)) (list "abc" 1 (list 4 5))))
; TODO (assert (equal? (vector "abc" 1) (vector "abc" 1)))

; not
(assert (not (eqv? #t #f)))
(assert (not (eqv? (list "abc") (list "abc"))))
(assert (not (eqv? (list "abc" 1) (list "abc" 1))))
(assert (not (eqv? (vector "abc" 1) (vector "abc" 1))))
(assert (not (eqv? not equal?))) ;; comparing procedures
(assert (not (eqv? (vector "abc" 1) (vector "abc" 1))))
(assert (not (eqv? not equal?))) ;; comparing procedures

(assert (not (equal? (list "abc") (list "abcd"))))
(assert (not (equal? (list "abc" 1) (list "abc" 2))))
; TODO (assert (not (equal? (vector "abc" 1) (vector "abc" 2))))
(assert (not (equal? not eqv?))) ;; comparing procedures
