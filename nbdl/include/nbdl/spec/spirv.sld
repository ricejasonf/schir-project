(import (schir builtins))

(define-library (nbdl spec spirv)
  (import (schir base)
          (schir mlir)
          (schir mlir all-passes)
          (schir clang)
          (nbdl spec))
  (begin
    (load-plugin "libNbdl.so")
    (define mark-shader-entry
      (load-builtin "nbdl_spec_mark_shader_entry"))
    (define %define-spirv-binary
      (load-builtin "nbdl_spec_define_spirv_binary"))
    (define %write-spirv-binary
      (load-builtin "nbdl_spec_write_spirv_binary"))

    (define %shader-global '%shader-global)

    ;; The builtin variables a shader-global may refer to.
    (define %shader-builtins
      '("Position" "PointSize" "FragCoord" "FragDepth" "FrontFacing"
        "VertexIndex" "InstanceIndex" "GlobalInvocationId"
        "LocalInvocationId" "LocalInvocationIndex" "WorkgroupId"
        "NumWorkgroups"))

    ;; Describe a resource or builtin variable of type T that is a parameter
    ;; or result of a shader entrypoint via one of the following.
    ;;   (shader-global 'uniform-buffer T)
    ;;   (shader-global 'builtin "Name" T)
    (define-syntax shader-global
      (syntax-rules ()
        ((shader-global Kind Arg ...)
         (%shader-global-aux (syntax-source-loc Kind) Kind (list Arg ...)))))

    (define (%shader-global-aux Loc Kind Args)
      (cond
        ((eq? Kind 'uniform-buffer)
         (unless (eqv? (length Args) 1)
           (error-with-loc Loc "expecting (shader-global 'uniform-buffer T)"))
         (list %shader-global Kind #f (car Args)))
        ((eq? Kind 'builtin)
         (unless (eqv? (length Args) 2)
           (error-with-loc Loc
             "expecting (shader-global 'builtin \"Name\" T)"))
         (unless (member (car Args) %shader-builtins)
           (error-with-loc Loc "unknown shader builtin: {}" (car Args)))
         (list %shader-global Kind (car Args) (cadr Args)))
        ((memq Kind '(storage-buffer sampled-texture storage-texture))
         (error-with-loc Loc "TODO shader-global kind is not supported yet: {}"
                         Kind))
        (else (error-with-loc Loc "unknown shader-global kind: {}" Kind))))

    (define (shader-global? Value)
      (and (pair? Value)
           (eq? %shader-global (car Value))))

    (define (shader-global-kind Global)
      (cadr Global))

    (define (shader-global-builtin Global)
      (car (cddr Global)))

    (define (shader-global-type Global)
      (cadr (cddr Global)))

    ;; Get the mlir type of a parameter or result of a shader entrypoint.
    (define (%shader-type T)
      (%cpp-or-type (if (shader-global? T) (shader-global-type T) T)))

    ;; Parse the stage of a shader entrypoint ("Vertex", "Fragment", or
    ;; ("Compute" X Y Z)) as (ExecutionModel LocalSize).
    (define (%parse-shader-stage Loc Stage)
      (cond
        ((equal? Stage "Vertex") (list "Vertex" #f))
        ((equal? Stage "Fragment") (list "Fragment" #f))
        ((equal? Stage "Compute")
         (error-with-loc Loc
           "compute stage requires a local size: (\"Compute\" X Y Z)"))
        ((and (pair? Stage)
              (equal? (car Stage) "Compute")
              (eqv? (length Stage) 4)
              (every (lambda (N) (and (exact-integer? N) (positive? N)))
                     (cdr Stage)))
         (list "GLCompute" (cdr Stage)))
        (else (error-with-loc Loc "unknown shader stage: {}" Stage))))

    ;; Write a string as a quoted string literal.
    (define (%quote String)
      (format-string "{}" String))

    ;; Describe the variable of each parameter (or result) type of a shader
    ;; entrypoint as a dictionary attribute string where a plain type is
    ;; at a Location and a uniform-buffer has its set and binding in the
    ;; SDL3 layout.
    (define (%shader-interface Loc Name Model Ts Input?)
      (define StorageClass (if Input? "Input" "Output"))
      (define UniformSet
        (cond
          ((equal? Model "Vertex") 1)
          ((equal? Model "Fragment") 3)
          (else 2)))
      (let Loop ((Ts Ts) (Location 0) (Binding 0) (Result '()))
        (if (null? Ts)
          (reverse Result)
          (let ((T (car Ts)))
            (when (eqv? (%shader-type T) !nbdl.unknown)
              (error-with-loc Loc
                "shader entrypoint types cannot be !nbdl.unknown: {}" Name))
            (cond
              ((not (shader-global? T))
               (Loop (cdr Ts) (+ Location 1) Binding
                     (cons (string-append
                             "{storage_class = " (%quote StorageClass)
                             ", location = " (number->string Location)
                             " : i32}")
                           Result)))
              ((eq? (shader-global-kind T) 'builtin)
               (Loop (cdr Ts) Location Binding
                     (cons (string-append
                             "{storage_class = " (%quote StorageClass)
                             ", built_in = "
                             (%quote (shader-global-builtin T)) "}")
                           Result)))
              (Input?
               (Loop (cdr Ts) Location (+ Binding 1)
                     (cons (string-append
                             "{storage_class = \"Uniform\", descriptor_set = "
                             (number->string UniformSet)
                             " : i32, binding = " (number->string Binding)
                             " : i32}")
                           Result)))
              (else
               (error-with-loc Loc
                 "shader entrypoint cannot return a uniform-buffer: {}"
                 Name)))))))

    ;; Map each FuncOp of a shader entrypoint to the attribute that
    ;; describes its entry point and its row of the C++ table of entry
    ;; points as (FuncOp Attr Row).
    (define %shader-entries '())

    ;; Get the entry point attribute of a FuncOp or #f.
    (define (%shader-entry-attr ShaderEntries FuncOp)
      (let ((Entry (assq FuncOp ShaderEntries)))
        (and Entry (cadr Entry))))

    ;; Write the initializer of a nbdl::shader_entry.
    (define (%shader-entry-row Name Model LocalSize ArgTs)
      (define NumUniformBuffers
        (let Loop ((Ts ArgTs) (N 0))
          (cond
            ((null? Ts) N)
            ((and (shader-global? (car Ts))
                  (eq? (shader-global-kind (car Ts)) 'uniform-buffer))
             (Loop (cdr Ts) (+ N 1)))
            (else (Loop (cdr Ts) N)))))
      (string-append
        "{" (%quote (string-copy Name 0)) ", ::nbdl::shader_stage::"
        (cond
          ((equal? Model "Vertex") "vertex")
          ((equal? Model "Fragment") "fragment")
          (else "compute"))
        ", 0, 0, 0, " (number->string NumUniformBuffers) ", {"
        (string-join (map number->string (or LocalSize '(0 0 0))) ", ")
        "}}"))

    ;; Create the func.func of a shader entrypoint via Thunk which receives
    ;; the symbol name and the parameter and result types, and record its
    ;; entry point to be created by finalize-module.
    (define (%define-shader-entrypoint Loc Name Stage ArgTs RetTs Thunk)
      (when (or (exported-as? Name 'cpp)
                (exported-as? Name 'c)
                (exported-as? Name 'c-internal))
        (error-with-loc Loc
          "shader entrypoint cannot be exported to C or C++: {}" Name))
      (unless (exported-as? Name 'shader)
        (error-with-loc Loc
          "shader entrypoint is not exported via export-shader: {}" Name))
      (let ()
        (define ModelAndLocalSize (%parse-shader-stage Loc Stage))
        (define Model (car ModelAndLocalSize))
        (define LocalSize (cadr ModelAndLocalSize))
        (define Inputs (%shader-interface Loc Name Model ArgTs #t))
        (define Outputs (%shader-interface Loc Name Model RetTs #f))
        (define EntryAttr
          (attr (string-append
                  "{name = " (%quote (string-copy Name 0))
                  ", execution_model = " (%quote Model) ", "
                  (if LocalSize
                    (string-append
                      "local_size = array<i32: "
                      (string-join (map number->string LocalSize) ", ")
                      ">, ")
                    "")
                  "inputs = [" (string-join Inputs ", ")
                  "], outputs = [" (string-join Outputs ", ") "]}")))
        (define FuncOp
          (top-level-op
            Loc
            Name
            (lambda (SymName)
              (Thunk SymName
                     (map %shader-type ArgTs)
                     (map %shader-type RetTs)))))
        (define Row (%shader-entry-row Name Model LocalSize ArgTs))
        (add-lower-op FuncOp)
        (set! %shader-entries
          (cons (list FuncOp EntryAttr Row) %shader-entries))
        (make-uncallable "shader entrypoint cannot be called: {}" Name)))

    ;; Define the entry point of a shader with the same typed syntax as
    ;; define-fn where Stage is "Vertex", "Fragment", or ("Compute" X Y Z).
    ;; A parameter (or result) type is a plain type for a stage input (or
    ;; output) or a shader-global for a resource or builtin variable.
    (define-syntax define-shader-entrypoint
      (syntax-rules (: ->)
        ((define-shader-entrypoint Name Stage ((Arg : ArgT) ...) -> (RetT ...)
           Body1 BodyN ...)
         (define Name
           (%define-shader-entrypoint
             (syntax-source-loc Name)
             'Name
             'Stage
             (list ArgT ...)
             (list RetT ...)
             (lambda (SymName ArgTs RetTs)
               (create-op
                 "func.func"
                 (loc: (syntax-source-loc Name))
                 (operands:)
                 (attributes:
                   ("sym_name" (string-attr SymName))
                   ("function_type"
                    (type-attr
                      (%function-type (apply vector ArgTs)
                                      (apply vector RetTs)))))
                 (result-types:)
                 (region: "body" ((Arg : (%shader-type ArgT)) ...)
                          Body1 BodyN ...))))))))

    ;; Track the SPIRV binary exported via export-spirv-binary as (Name Loc)
    ;; or #f.
    (define %spirv-binary #f)

    ;; Export the SPIRV binary of the functions exported via export-shader
    ;; as a C function Name that returns its words as a
    ;; nbdl::memref<std::int32_t, 1> and a C++ table Name_entries of
    ;; nbdl::shader_entry for each entry point.
    (define-syntax export-spirv-binary
      (syntax-rules ()
        ((export-spirv-binary Name)
         (%export-spirv-binary (syntax-source-loc Name) 'Name))))

    (define (%export-spirv-binary Loc Name)
      (when %spirv-binary
        (error-with-loc Loc "spirv binary is already exported: {}"
                        (car %spirv-binary)))
      (set! %spirv-binary (list Name Loc)))

    (define %write-spirv-module? #f)
    (define %spirv-binary-path #f)

    ;; Write the module of functions lowered to SPIRV by finalize-module
    ;; (for debugging and testing.)
    (define (write-spirv-module)
      (set! %write-spirv-module? #t))

    ;; Write the SPIRV binary of finalize-module to a file
    ;; (for debugging and testing.)
    (define (write-spirv-binary Path)
      (unless (string? Path)
        (error "expecting path string: {}" Path))
      (set! %spirv-binary-path Path))

    ;; Copy the lower-module copy of each FuncOp into a new module and
    ;; mark each shader entrypoint to have an entry point created by
    ;; nbdl-to-spirv.
    (define (%lower-to-spirv ShaderOps ShaderEntries)
      (define SPIRVModule (create-top-module "nbdl_spec_spirv_module"))
      (let Loop ((Ops ShaderOps))
        (unless (null? Ops)
          (let ((Copy (copy-to-module SPIRVModule
                                      (lower-module-copy (car Ops))))
                (EntryAttr (%shader-entry-attr ShaderEntries (car Ops))))
            (when EntryAttr
              (mark-shader-entry Copy EntryAttr)))
          (Loop (cdr Ops))))
      SPIRVModule)

    ;; Define the function that returns the SPIRV binary and write its
    ;; C++ wrapper and table of entry points.
    (define (%finalize-spirv-binary Module SPIRVModule SPIRVBinary
                                    ShaderOps ShaderEntries)
      (define Name (car SPIRVBinary))
      (define Loc (cadr SPIRVBinary))
      (define Rows
        (let Loop ((Rest ShaderOps) (Result '()))
          (if (null? Rest)
            (reverse Result)
            (let ((Entry (assq (car Rest) ShaderEntries)))
              (Loop (cdr Rest)
                    (if Entry (cons (car (cddr Entry)) Result) Result))))))
      (let ((FuncOp (%define-spirv-binary SPIRVModule (finalize-llvm-module)
                                          (namespace-prefix Name) Loc)))
        (mark-c-adapter FuncOp)
        ((emit-cpp Module define-llvm-wrapper) FuncOp)
        (write-lexer
          (string-append
            "inline constexpr ::std::array<::nbdl::shader_entry, "
            (number->string (length Rows)) "> "
            (string-copy Name 0) "_entries = {{"
            (string-join Rows ", ") "}};"))
        (flush-tokens)))

    ;; Save and reset the state of the module being finalized and return
    ;; the procedure that lowers its functions exported via export-shader
    ;; to SPIRV.
    (define (%begin-finalize ShaderOps)
      (define ShaderEntries %shader-entries)
      (define SPIRVBinary %spirv-binary)
      (define WriteSPIRV? %write-spirv-module?)
      (define SPIRVBinaryPath %spirv-binary-path)
      (set! %shader-entries '())
      (set! %spirv-binary #f)
      (set! %write-spirv-module? #f)
      (set! %spirv-binary-path #f)
      (when (and SPIRVBinary (null? ShaderOps))
        (error-with-loc (cadr SPIRVBinary)
          "spirv binary has no functions exported via export-shader: {}"
          (car SPIRVBinary)))
      (lambda (Module)
        (unless (null? ShaderOps)
          (let ((SPIRVModule (%lower-to-spirv ShaderOps ShaderEntries)))
            (run-passes SPIRVModule "nbdl-to-spirv")
            (when WriteSPIRV?
              (write SPIRVModule)
              (newline))
            (when SPIRVBinaryPath
              (%write-spirv-binary SPIRVModule SPIRVBinaryPath))
            ;; Lower the SPIRV binary to LLVM with the other functions.
            (when SPIRVBinary
              (%finalize-spirv-binary Module SPIRVModule SPIRVBinary
                                      ShaderOps ShaderEntries))))))

    (register-shader-backend %begin-finalize)

    ) ; end of... begin
  (export
    define-shader-entrypoint
    shader-global
    export-spirv-binary
    write-spirv-module
    write-spirv-binary
    )
)  ; end of (nbdl spec spirv)
