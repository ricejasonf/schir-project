#!curly-infix
(import (schir base))

;; Check the mutable visits of nbdl functions with a Schlap rule.
(define-library (nbdl spec schlap)
  (export check-mut-visits)
  (import (except (schir base) < > + - *)
          (schlap base)
          (schlap infix)
          (schlap rule)
          (only (nbdl spec) named-fn-op))
  (begin
    (define-rule MutVisit
      (variables invalid)
      (init {invalid = ∅})

      ;; Invalidate every value read from the root of a mutated value.
      (on-mutate (v)
        {(′ invalid) = {invalid ∪ (read-from (root v))}})

      ;; Require a used value to be valid.
      (on-use (v)
        (require {v ∉ invalid})))

    ;; Check the mutable visits of a function.
    (define-syntax check-mut-visits
      (syntax-rules ()
        ((_ Fn)
         (check-rule* (syntax-source-loc) MutVisit (named-fn-op Fn)))))))
