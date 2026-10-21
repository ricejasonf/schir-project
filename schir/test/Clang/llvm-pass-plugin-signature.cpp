// RUN: not clang++ -std=c++26 -I %schir_module_path -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so %s -S -emit-llvm -o /dev/null 2>&1 \
// RUN:   | FileCheck %s

// The C ABI passes vec2 as double, so the injected definition taking
// <2 x float> does not match the declaration.
// CHECK: error: Schir injected function 'injected_vec2' has type '<2 x float> (<2 x float>)' but the module expects 'double (double)'
// CHECK-NOT: injected_i32

using vec2 = float __attribute__((ext_vector_type(2)));
extern "C" vec2 injected_vec2(vec2);
extern "C" int injected_i32(int);

#pragma schir_scheme
{
(import (schir base)
        (schir mlir)
        (schir mlir all-passes)
        (schir llvm pass))

(load-dialect "llvm")
(define i32 (type "i32"))
(define v2f32 (type "vector<2xf32>"))

(define (create-identity Name T)
  (create-op "func.func"
    (loc: 0)
    (operands:)
    (attributes:
      ("sym_name" (string-attr Name))
      ("function_type"
       (type-attr (%function-type (vector T) (vector T)))))
    (result-types:)
    (region: "body" ((X : T))
      (create-op "llvm.return"
        (loc: 0)
        (operands: X)
        (attributes:)
        (result-types:)))))

(define Module
  (create-op "builtin.module"
    (loc: 0)
    (operands:)
    (attributes:)
    (result-types:)
    (region: "body" ()
      (create-identity "injected_vec2" v2f32)
      (create-identity "injected_i32" i32))))
(register-all-passes)
(run-passes Module "convert-func-to-llvm")
(inject-module Module)
}

vec2 call_vec2(vec2 x) { return injected_vec2(x); }
int call_i32(int x) { return injected_i32(x); }
