; RUN: not schir-scheme --module-path=%schir_module_path %s 2>&1 | FileCheck %s
(import (schir builtins))
(import (schir mlir))

(load-dialect "func")

(define i32 (type "i32"))

(define good-func
  (create-op "func.func"
    (loc: 0)
    (operands:)
    (attributes:
      ("sym_name" (string-attr "good_func"))
      ("function_type" (type-attr (%function-type #() #()))))
    (result-types:)
    (region: "body" ()
      (create-op "func.return" (loc: 0) (operands:) (attributes:)
        (result-types:)))))

(if (verify good-func)
  'ok
  (error "expect good-func to pass verification"))

(define bad-func
  (create-op "func.func"
    (loc: 0)
    (operands:)
    (attributes:
      ("sym_name" (string-attr "bad_func"))
      ("function_type" (type-attr (%function-type #(i32) #()))))
    (result-types:)
    (region: "body" ((arg0 : i32))
      (create-op "func.return"
        (loc: 0)
        (operands: arg0)
        (attributes:)
        (result-types:)))))

(verify bad-func)

; CHECK: verify-op.scm:26:13: error: operation failed verification
; CHECK: verify-op.scm:34:17: note: 'func.return' op has 1 operands, but enclosing function (@bad_func) returns 0
