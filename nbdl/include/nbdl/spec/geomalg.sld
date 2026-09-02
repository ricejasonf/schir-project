(import (schir builtins))

(define-library (nbdl spec geomalg)
  (export define-geomalg-fn)
  (import (nbdl spec)
          (prefix (geomalg base) geomalg-))
  (begin
    ; Just force CGA metric for now since it is the only use case.
    (geomalg-with-metric 'cga)

    (define-syntax define-geomalg-fn
      (syntax-rules ()
        ((define-geomalg-fn Name ((ArgName : ArgType) ...) BodyI ... BodyN)
         (define Name
           (make-named-fn
             'Name
             (top-level-op
               'Name
               (lambda ()
                 (geomalg-define-func-aux
                   Name ((ArgName : ArgType) ...)
                   BodyI ...
                   BodyN))))))))

    ));
