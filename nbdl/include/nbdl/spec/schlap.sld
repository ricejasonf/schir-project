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
    ;; Assume that the stores form a rooted tree, that a mutation
    ;; invalidates only the values below the mutated store, and that
    ;; distinct arguments are distinct roots.
    (define-rule MutVisit
      (variables invalid)
      (init {invalid = ∅})

      ;; Require a mutated value to be valid and invalidate the values it
      ;; may own unless the mutation is child-stable.
      (on-mutate (v)
        (require {v ∉ invalid})
        {(′ invalid) = {invalid ∪ (ite (child-stable v) ∅ (owned-by v))}})

      ;; Require a used value to be valid.
      (on-use (v)
        (require {v ∉ invalid})))

    ;; Check the mutable visits of a function.
    (define-syntax check-mut-visits
      (syntax-rules ()
        ((_ Fn)
         (check-rule* (syntax-source-loc) MutVisit (named-fn-op Fn)))))))
