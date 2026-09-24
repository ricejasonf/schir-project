(import (schir builtins))

(define-library (nbdl spec geomalg)
  (export define-geomalg-fn
          build-geomalg-exports)
  (import (schir base)
          (schir llvm pass)
          (schir mlir)
          (schir mlir all-passes)
          (nbdl spec)
          (prefix (geomalg base) geomalg-))
  (begin
    ;; Just force CGA metric for now since it is the only use case.
    (geomalg-with-metric 'cga)

    ;; Create modules for llvm and spirv targets.
    ; TODO I think we want these in a helper library
    ;      and have export-c and export-shader copy
    ;      the original FuncOp and lower via a provided
    ;      callback (or maybe a registed lowering pass that
    ;                is applied to the whole module.)
    (define llvm-module (create-top-module "nbdl_spec_geomalg_llvm_module"))
    (define spirv-module (create-top-module "nbdl_spec_geomalg_spirv_module"))
    (inject-module llvm-module)

    ; TODO Use top-level-op.
    (define-syntax define-geomalg-fn
      (syntax-rules ()
        ((define-geomalg-fn Name ((ArgName : ArgType) ...) BodyI ... BodyN)
         (define Name
           (let ((FuncOp (with-module-builder
                           llvm-module
                           (lambda ()
                             (geomalg-define-func-aux
                               Name ((ArgName : ArgType) ...)
                               BodyI ...
                               BodyN)))))
             ; Run geomalg-lower pass.
             (run-passes FuncOp
                         "func.func(geomalg-expand-func{metric=cga})")
             (make-named-fn 'Name (declare-func FuncOp)))))))

    ; TODO Replace this with the export-c stuff.
    ;      geomalg-to-llvm would need to be applicable to a monolithic module.
    (define (build-geomalg-exports)
      (run-passes llvm-module "geomalg-to-llvm"))

    ));
