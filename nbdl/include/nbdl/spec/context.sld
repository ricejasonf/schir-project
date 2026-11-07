(import (schir base))

(define-library (nbdl spec context)
  (import (schir base)
          (nbdl spec))
  (begin
    (load-plugin "libNbdl.so")
    (define %path-type
      (load-builtin "nbdl_spec_path_type"))
    (define path-prefix?
      (load-builtin "nbdl_spec_is_path_prefix"))
    (define paths-may-alias?
      (load-builtin "nbdl_spec_paths_may_alias"))

    ;; Denote the root of an application's state tree
    ;; defined via define-context.
    (define-record-type <context>
      (make-context Struct Members)
      context?
      (Struct context-struct)
      (Members context-members))

    ;; Denote a member of a context declared via define-member.
    (define-record-type <context-member>
      (make-context-member Key Type Store Loc)
      context-member?
      (Key context-member-key)
      (Type context-member-type)
      (Store context-member-store)
      (Loc context-member-loc))

    ;; Hold the members of the current define-context body
    ;; in reverse order or #f outside of a body.
    (define %current-members #f)

    ;; Find the member with Key in Members or return #f.
    (define (%find-member Key Members)
      (cond
        ((null? Members) #f)
        ((equal? Key (context-member-key (car Members))) (car Members))
        (else (%find-member Key (cdr Members)))))

    ;; Add a member to the current define-context body.
    (define (%add-member Loc Key Type Store)
      (unless %current-members
        (error-with-loc Loc "define-member outside of define-context: {}"
                        Key))
      (when (%find-member Key %current-members)
        (error-with-loc Loc "context member defined more than once: {}"
                        Key))
      (set! %current-members
        (cons (make-context-member Key Type Store Loc) %current-members)))

    ;; Call BodyProc with Params and return the members it declares
    ;; in declaration order.
    (define (%run-context-body BodyProc Params)
      (define Saved %current-members)
      (dynamic-wind
        (lambda () (set! %current-members '()))
        (lambda ()
          (apply BodyProc Params)
          (reverse %current-members))
        (lambda () (set! %current-members Saved))))

    ;; Define a context named Name whose body BodyProc is called
    ;; with the init params.
    (define (%make-context Loc Name InitParamCount BodyProc)
      (define Members #f)
      (define Struct
        (make-struct
          Loc
          Name
          InitParamCount
          (lambda Params
            (set! Members (%run-context-body BodyProc Params))
            (map (lambda (Member)
                   (list (context-member-key Member)
                         (context-member-store Member)
                         (context-member-loc Member)))
                 Members))))
      (make-context Struct Members))

    ;; Define a context where the formals are the constructor params.
    (define-syntax define-context
      (syntax-rules ()
        ((define-context Name (Formals ...) Body ...)
         (define Name
           (%make-context
             (syntax-source-loc Name)
             'Name
             (length '(Formals ...))
             (lambda (Formals ...) Body ... #t))))))

    ;; Declare a member of the current context constructed
    ;; as Type with the init args.
    (define-syntax define-member
      (syntax-rules ()
        ((define-member Key Type InitArg ...)
         (%add-member
           (syntax-source-loc Key)
           Key
           Type
           (store Type (init-args: InitArg ...))))))

    ;; Get the !nbdl.path type of a path whose keys are member names,
    ;; tags, or nested paths from the same root store.
    (define (path-type Path)
      (define Root (path-spec-root Path))
      (apply %path-type
             (map (lambda (Node)
                    (define Key (car Node))
                    (cond
                      ((member-name-literal? Key)
                       (member-name-type (string-copy (symbol->string Key) 1)))
                      ((tag-type? Key) Key)
                      ((and (path-spec? Key) (eq? (path-spec-root Key) Root))
                       (path-type Key))
                      (else
                       (error-with-loc (cdr Node)
                         "expecting a member name, a tag, or a nested path from the same root as a path key: {}"
                         Key))))
                  (path-spec-nodes Path))))

    ) ; end of... begin
  (export
    define-context
    define-member
    path-type
    path-prefix?
    paths-may-alias?
    context?
    context-struct
    context-members
    context-member-key
    context-member-type
    context-member-loc
    )
)  ; end of (nbdl spec context)
