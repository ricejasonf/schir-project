; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir builtins))
(import (schir mlir))

(load-dialect "func")
(load-dialect "schir")

(define !schir.context (type "!schir.context"))
(define !schir.value (type "!schir.value"))

; CHECK: #op{%0 = "schir.literal"() {info = #schir<"\22foo\22">} : () -> !schir.value
(write
  (create-op "schir.literal"
    (loc: 0)
    (operands:)
    (attributes:
      ("info" (value-attr "foo")))
    (result-types: !schir.value)))

(newline)

; CHECK: #op{%0 = "schir.literal"() {info = #schir<"5">} : () -> !schir.value
(write
  (create-op "schir.literal"
    (loc: 0)
    (operands:)
    (attributes:
      ("info" (value-attr 5)))
    (result-types: !schir.value)
    ))

(newline)

; CHECK: #op{%0 = "schir.literal"() {info = #schir<"5000">} : () -> !schir.value
(write
  (create-op "schir.literal"
    (loc: 0)
    (operands:)
    (attributes:
      ("info" (attr "#schir<\"5000\">" !schir.value)))
    (result-types: !schir.value)))

(define the-answer
  (create-op "schir.literal"
    (loc: 0)
    (operands:)
    (attributes:
      ("info" (value-attr 41)))
    (result-types: !schir.value)))

(newline)
; CHECK: non-parent:()
(write 'non-parent:)
(write (parent-op the-answer))

(newline)

; CHECK: "schir.command"() ({
; CHECK-NEXT: %0 = "schir.load_global"() <{name = @_SCHIRL5SschirL4SbaseV5Swrite
; CHECK-NEXT: %1 = "schir.literal"() {info = #schir<"42">}
; CHECK-NEXT: "schir.apply"(%0, %1) : (!schir.value, !schir.value) -> ()
; CHECK-NEXT: }) : () -> ()
(define the-number-one 1)

(define command
  (create-op "schir.command"
    (loc: 0)
    (operands:)
    (attributes:)
    (result-types:)
    (region: "body" ()
      (define callee
        (create-op "schir.load_global"
          (loc: 0)
          (operands:)
          (attributes:
            ("name" (flat-symbolref-attr "_SCHIRL5SschirL4SbaseV5Swrite")))
          (result-types: !schir.value)
          ))
      (define arg1
        (create-op "schir.literal"
          (loc: 0)
          (operands:)
          (attributes:
            ("info" (value-attr 42)))
          (result-types: !schir.value)))
      (create-op "schir.apply"
        (loc: 0)
        (operands: (result callee) (result arg1))
        (attributes:)
        (result-types:))
      )))
(write command)
(newline)

;; FIXME Why is it not pretty printing the func.func?
; COM-CHECK: #op{func.func @my_func(%arg0: !schir.context, %arg1: !schir.value) {

; CHECK: #op{"func.func"() <{
; CHECK-NEXT: ^bb0(%arg0: !schir.context, %arg1: !schir.value)
(define my-func
  (create-op "func.func"
    (loc: 0)
    (operands:)
    (attributes:
      ("sym_name" (string-attr "my_func"))
      ("function_type"
       (type-attr (%function-type
                    #(!schir.context !schir.value)
                    #()))))
    (result-types:)
    (region: "body" ((ctx : !schir.context) (arg1 : !schir.value))
      (define callee
        (create-op "schir.load_global"
          (loc: 0)
          (operands:)
          (attributes:
            ("name" (flat-symbolref-attr "_SCHIRL5SschirL4SbaseV5Swrite")))
          (result-types: !schir.value)
          ))
      (create-op "schir.apply"
        (loc: 0)
        (operands: ctx arg1)
        (attributes:)
        (result-types:))
      )))
(write my-func)
(newline)

