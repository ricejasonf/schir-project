// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs \
// RUN:   -fplugin=SchirClang.so -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -S -emit-llvm -o - | FileCheck %s

// CHECK-LABEL: @_Z15normal_cpp_funcv
// CHECK-LABEL: @injected_func
// CHECK-NEXT ret i32 42

#pragma schir_scheme
{
(import (schir base)
        (schir mlir)
        (schir mlir all-passes)
        (schir llvm pass))

(load-dialect "llvm")
(define i32 (type "i32"))

(define Module
  (create-op "builtin.module"
    (loc: 0)
    (operands:)
    (attributes:)
    (result-types:)
    (region: "body" ()
      (create-op "func.func"
        (loc: 0)
        (operands:)
        (attributes:
          ("sym_name" (string-attr "injected_func"))
          ("function_type"
           (type-attr (%function-type #() #(i32)))))
        (result-types:)
        (region: "body" ()
          (define The42
            (result
              (create-op "llvm.mlir.constant"
                (loc: 0)
                (operands:)
                (attributes:
                  ("value" (attr "42" i32)))
                (result-types: i32))))
          (create-op "llvm.return"
            (loc: 0)
            (operands: The42)
            (attributes:)
            (result-types:)))))))
(register-all-passes)
(run-passes Module "convert-func-to-llvm")
(inject-module Module)
}

int normal_cpp_func() { return 5; }
