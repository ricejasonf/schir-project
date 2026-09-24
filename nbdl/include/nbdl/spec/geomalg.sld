(import (schir builtins))

(define-library (nbdl spec geomalg)
  (export define-geomalg-fn)
  (import (schir base)
          (schir llvm pass)
          (schir mlir)
          (schir mlir all-passes)
          (nbdl spec)
          (prefix (geomalg base) geomalg-))
  (begin
    ;; Create modules for llvm and spirv targets.
    ; TODO I think we want these in a helper library
    ;      and have export-c and export-shader copy
    ;      the original FuncOp and lower via a provided
    ;      callback (or maybe a registed lowering pass that
    ;                is applied to the whole module.)
    (define llvm-module (create-top-module "nbdl_spec_geomalg_llvm_module"))
    (define spirv-module (create-top-module "nbdl_spec_geomalg_spirv_module"))
    (inject-module llvm-module)

    ;; Define the func in the main module where it is expanded so
    ;; visits are validated against it. A copy is lowered in the
    ;; llvm-module which is injected into the translation unit.
    (define-syntax define-geomalg-fn
      (syntax-rules ()
        ((define-geomalg-fn Name ((ArgName : ArgType) ...) BodyI ... BodyN)
         (define Name
           (let ((FuncOp (top-level-op 'Name
                                       (lambda ()
                                         (geomalg-define-func-aux
                                           Name ((ArgName : ArgType) ...)
                                           BodyI ...
                                           BodyN)))))
             ;; Just force CGA metric for now since it is the only use case.
             (run-passes (parent-op FuncOp)
                         (string-append
                           "geomalg-expand{metric=cga func-name="
                           'Name
                           "}"))
             (with-module-builder llvm-module
                                  (lambda () (copy-op FuncOp)))
             ; TODO geomalg-to-llvm is applied to the whole module
             ;      which includes previously lowered functions.
             (run-passes llvm-module "geomalg-to-llvm")
             (make-named-fn 'Name FuncOp))))))

    ));
