; RUN: not schir-scheme %s 2>&1 | FileCheck %s
(import (schir builtins))

; CHECK: error-with-loc.scm:20:3: error: requirement not satisfied: string? 5
; CHECK: error-with-loc.scm:12:11: note: error raised here
(define-syntax require
  (syntax-rules ()
    ((require Pred Val)
     ((lambda (ValResult)
        (if (Pred ValResult)
          X
          (error-with-loc
            (syntax-source-loc) ; Test getting the syntax keyword loc.
            "requirement not satisfied: {} {}" 'Pred ValResult
            )))
      Val))))

(define (check-stuff)
  (require number? 5)
  (require string? 5))

(check-stuff)
