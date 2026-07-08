(import (schir builtins))

(define-library (schir mangle)
  (export mangle-module
          mangle-variable-name
          mangle-function-name
          parse-library-spec
          parse-module-prefix)
  (import (schir builtins))
  (begin
    (load-plugin "libSchirMangle.so")
    (define mangle-module
      (load-builtin "schir_mangle_module"))
    (define mangle-variable-name
      (load-builtin "schir_mangle_variable_name"))
    (define mangle-function-name
      (load-builtin "schir_mangle_function_name"))
    (define parse-library-spec
      (load-builtin "schir_parse_library_spec"))
    (define parse-module-prefix
      (load-builtin "schir_parse_module_prefix"))))
