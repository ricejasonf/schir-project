; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir base))
(import (schir mlir))

(load-dialect "func")

(define i32 (type "i32"))
(define f32 (type "f32"))

(define (values->vector Thunk)
  (call-with-values Thunk vector))

(define main-module (create-top-module "main_module"))
(define other-module (create-top-module "other_module"))

(define my-func
  (with-module-builder main-module
    (lambda ()
      (create-op "func.func"
        (loc: 0)
        (operands:)
        (attributes:
          ("sym_name" (string-attr "my_func"))
          ("function_type" (type-attr (%function-type #(i32 f32) #(f32)))))
        (result-types:)
        (region: "body" ((x : i32) (y : f32))
          (create-op "func.return"
            (loc: 0)
            (operands: y)
            (attributes:)
            (result-types:)))))))

; CHECK: inputs:#(#mlir.type{i32} #mlir.type{f32})
(write 'inputs:)
(write (values->vector (lambda () (function-type-inputs my-func))))
(newline)

; CHECK-NEXT: results:#(#mlir.type{f32})
(write 'results:)
(write (values->vector (lambda () (function-type-results my-func))))
(newline)

;; Create an external func.func (ie a forward declaration.)
(define (declare-func FuncOp SymName)
  (create-op "func.func"
    (loc: 0)
    (operands:)
    (attributes:
      ("sym_name" (string-attr SymName))
      ("function_type"
        (type-attr
          (%function-type
            (values->vector (lambda () (function-type-inputs FuncOp)))
            (values->vector (lambda () (function-type-results FuncOp))))))
      ("sym_visibility" (string-attr "private")))
    (result-types:)
    (region: "body" ())))

(define my-func-decl
  (with-module-builder main-module
    (lambda () (declare-func my-func "my_func_decl"))))

(verify main-module)

;; Copy both into another module.
(define my-func-copy
  (with-module-builder other-module
    (lambda () (copy-op my-func))))
(define my-func-decl-copy
  (with-module-builder other-module
    (lambda () (copy-op my-func-decl))))

(verify other-module)

; CHECK-NEXT: distinct:#t#t
(write 'distinct:)
(write (not (eq? my-func my-func-copy)))
(write (not (eq? my-func-decl my-func-decl-copy)))
(newline)

; CHECK-NEXT: parents:#t#t
(write 'parents:)
(write (eq? (parent-op my-func-copy) other-module))
(write (eq? (parent-op my-func-decl-copy) other-module))
(newline)

; CHECK-NEXT: lookup:#t#t
(write 'lookup:)
(write (eq? (module-lookup other-module "my_func") my-func-copy))
(write (eq? (module-lookup other-module "my_func_decl") my-func-decl-copy))
(newline)

;; The originals remain in the main module.
; CHECK: #op{module @main_module {
; CHECK-NEXT:   func.func @my_func(%arg0: i32, %arg1: f32) -> f32 {
; CHECK-NEXT:     return %arg1 : f32
; CHECK-NEXT:   }
; CHECK-NEXT:   func.func private @my_func_decl(i32, f32) -> f32
; CHECK-NEXT: }
(write main-module)
(newline)

; CHECK: #op{module @other_module {
; CHECK-NEXT:   func.func @my_func(%arg0: i32, %arg1: f32) -> f32 {
; CHECK-NEXT:     return %arg1 : f32
; CHECK-NEXT:   }
; CHECK-NEXT:   func.func private @my_func_decl(i32, f32) -> f32
; CHECK-NEXT: }
(write other-module)
(newline)
