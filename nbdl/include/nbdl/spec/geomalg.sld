(import (schir builtins))

(define-library (nbdl spec geomalg)
  (export define-geomalg-fn)
  (import (nbdl spec)
          (prefix (geomalg base) geomalg-))
  (begin
    ;; Just force CGA metric for now since it is the only use case.
    (geomalg-with-metric 'cga)

    ; TODO If the func name is not in the export-c list,
    ;      then use an anonymous, mangled name for the
    ;      function, and add that name to the export-c list
    ;      so it ends up in the translation unit.
    ;      Do the same for export-shader and we will use
    ;      the symbol-dce pass to remove unused functions.
    ;      When we lower, if there are function names in
    ;      export-shader, we copy all of the names to a new
    ;      module to generate the shader program.
    ;      The resulting binary should be available to the application
    ;      (like a compile-time store or something.)
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
