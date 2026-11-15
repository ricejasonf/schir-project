; RUN: schir-scheme --mode=repl-check --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))

(define (id x) x)

; CHECK: #()
#()

; CHECK: #(5)
#(5)

; CHECK: #(42)
#((id 42))

; CHECK: #(1 2 "yo" "moo")
#(1 2 "yo" (id "moo"))

; CHECK: #(#(1 2 "yo" "moo"))
#(#(1 2 "yo" (id "moo")))

; CHECK: #()
(vector)

; CHECK: #(5)
(vector 5)

; CHECK: #(42)
(vector (id 42))

; CHECK: #(1 2 "yo" "moo")
(vector 1 2 "yo" (id "moo"))

; CHECK: #(#(1 2 "yo" "moo"))
(vector (vector 1 2 "yo" (id "moo")))

; CHECK-NEXT: #()
(make-vector 0)

; CHECK-NEXT: #(0)
(make-vector 1 0)

; CHECK-NEXT: #(0 0 0 0 0)
(make-vector 5 0)

; CHECK-NEXT: #(foo bar baz)
(let ((Vec (make-vector 3)))
  (vector-set! Vec 0 'foo)
  (vector-set! Vec 1 'bar)
  (vector-set! Vec 2 'baz)
  Vec
  )

; CHECK-NEXT: #(foo bar 0)
(let ((Vec (make-vector 3 0)))
  (vector-set! Vec 0 'foo)
  (vector-set! Vec 1 'bar)
  Vec
  )

; CHECK-NEXT: #(0 1 2 3 4)
(vector (vector-length #())
        (vector-length #(1))
        (vector-length #(1 2))
        (vector-length #(1 2 3))
        (vector-length #(1 2 3 4))
        )

; CHECK-NEXT: (baz bar foo)
(let ((Vec (vector 'foo 'bar 'baz)))
  (list (vector-ref Vec 2)
        (vector-ref Vec 1)
        (vector-ref Vec 0)))

; CHECK-NEXT: ((foo bar baz) (bar baz) (bar) ())
(let ((Vec (vector 'foo 'bar 'baz)))
  (list (vector->list Vec)
        (vector->list Vec 1)
        (vector->list Vec 1 2)
        (vector->list (vector))))

; CHECK-NEXT: (#(1 2 3) #())
(list (list->vector '(1 2 3))
      (list->vector '()))
