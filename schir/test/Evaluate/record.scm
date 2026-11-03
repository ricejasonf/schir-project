; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define-record-type <point>
  (make-point x y)
  point?
  (x point-x set-point-x!)
  (y point-y))

(define-record-type <other>
  (make-other x y)
  other?
  (x other-x)
  (y other-y))

(define P (make-point 1 2))

; CHECK: (1 2)
(write (list (point-x P) (point-y P)))
(newline)

; CHECK-NEXT: (#t #f #f #f #f)
(write (list (point? P)
             (point? (make-other 1 2))
             (point? (vector 1 2))
             (point? #())
             (point? '(1 2))))
(newline)

; CHECK-NEXT: 5
(set-point-x! P 5)
(write (point-x P))
(newline)

; Initialize fields in constructor order, leaving the rest #f.
(define-record-type <node>
  (make-node value)
  node?
  (next node-next set-node-next!)
  (value node-value))

; CHECK-NEXT: (a #f)
(define N (make-node 'a))
(write (list (node-value N) (node-next N)))
(newline)

; Shadow free identifiers of the expansion with field names.
(define-record-type <wrapper>
  (make-wrapper list vector)
  wrapper?
  (list wrapper-list)
  (vector wrapper-vector))

; CHECK-NEXT: (1 2)
(write (let ((W (make-wrapper 1 2)))
         (list (wrapper-list W) (wrapper-vector W))))
(newline)

; Define a record type in a body.
(define (local-record)
  (define-record-type <pair2>
    (make-pair2 a b)
    pair2?
    (a pair2-a)
    (b pair2-b))
  (let ((Q (make-pair2 'l 'r)))
    (list (pair2? Q) (pair2-a Q) (pair2-b Q))))

; CHECK-NEXT: (#t l r)
(write (local-record))
(newline)

; Share the record type of a body definition between evaluations.
(define (make-local-type)
  (define-record-type <box>
    (make-box v)
    box?
    (v box-v))
  (list make-box box?))

; CHECK-NEXT: (#t #t)
(write (let ((A (make-local-type))
             (B (make-local-type)))
         (let ((Box ((car A) 1)))
           (list ((cadr A) Box) ((cadr B) Box)))))
(newline)

; Give same-named record types of different bodies distinct types.
(define (make-other-local-type)
  (define-record-type <box>
    (make-box v)
    box?
    (v box-v))
  (list make-box box?))

; CHECK-NEXT: (#t #f)
(write (let ((A (make-local-type))
             (B (make-other-local-type)))
         (let ((Box ((car A) 1)))
           (list ((cadr A) Box) ((cadr B) Box)))))
(newline)

; Keep records disjoint from vectors.
; CHECK-NEXT: (#f #f #t)
(write (list (vector? P) (point? (vector <point> 1 2)) (symbol? <point>)))
(newline)

(define-record-type <thing>
  (make-thing a)
  thing?
  (a thing-a))

; CHECK-NEXT: #<record _SCHIRVlt5Sthinggt (1 2)>
(write (make-thing '(1 2)))
(newline)

; CHECK-NEXT: "expecting record of type <point>: #<record _SCHIRVlt5Sothergt 1 2>"
(write (guard (e ((error-object? e) (error-object-message e)))
         (point-x (make-other 1 2))))
(newline)

; CHECK-NEXT: "record constructor for <point> expects 2 arguments: (1)"
(write (guard (e ((error-object? e) (error-object-message e)))
         (make-point 1)))
(newline)

; CHECK-NEXT: (<point> 1)
(write (guard (e ((error-object? e) (error-object-irritants e)))
         (set-point-x! 1 2)))
(newline)
