// RUN: geomalg-opt --geomalg-expand %s | FileCheck %s

// RUN: geomalg-opt --geomalg-to-llvm %s | FileCheck --check-prefix=LLVM %s

// Values narrowed by simplification are widened to
// keep the result types of functions.

!e1 = !geomalg.blade<1>
!e2 = !geomalg.blade<2>
!vec2 = !geomalg.multivector<<1>, <2>>

// A multivector narrowed to a blade is widened with
// a zero coefficient for the missing blade.
// CHECK-LABEL: func.func @widen_blade
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>>
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK-SAME: -> !geomalg.blade<2>
// CHECK: [[SUM:%[0-9]+]] = "geomalg.sum"(%{{[0-9]+}}, [[ZERO]])
// CHECK-NEXT: geomalg.return [[SUM]] : !geomalg.multivector<<1>, <2>>
// LLVM-LABEL: llvm.func @widen_blade
// LLVM-SAME: -> vector<2xf32>
func.func @widen_blade(%arg0: !e1, %arg1: !e2) -> !vec2 {
  %0 = "geomalg.negate"(%arg1) : (!e2) -> !e2
  %1 = "geomalg.sum"(%arg0, %arg1, %0) : (!e1, !e2, !e2) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// A zero is widened to a blade with a zero coefficient.
// CHECK-LABEL: func.func @widen_zero
// CHECK-SAME: -> !geomalg.blade<1>
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK-SAME: -> !geomalg.blade<1>
// CHECK-NEXT: geomalg.return [[ZERO]] : !geomalg.blade<1>
// LLVM-LABEL: llvm.func @widen_zero
// LLVM-SAME: -> f32
func.func @widen_zero(%arg0: !e1) -> !e1 {
  %0 = "geomalg.negate"(%arg0) : (!e1) -> !e1
  %1 = "geomalg.sum"(%arg0, %0) : (!e1, !e1) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// A call argument narrowed by simplification is widened
// to keep the parameter types of the callee.
// CHECK-LABEL: func.func @widen_call_arg
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK-SAME: -> !geomalg.blade<2>
// CHECK: [[SUM:%[0-9]+]] = "geomalg.sum"(%{{[0-9]+}}, [[ZERO]])
// CHECK-NEXT: geomalg.call @widen_call_arg_callee([[SUM]])
// CHECK-SAME: (!geomalg.multivector<<1>, <2>>)
// LLVM-LABEL: llvm.func @widen_call_arg
func.func @widen_call_arg_callee(%arg0: !vec2) -> !vec2 {
  geomalg.return %arg0 : !vec2
}

func.func @widen_call_arg(%arg0: !e1, %arg1: !e2) -> !vec2 {
  %0 = "geomalg.negate"(%arg1) : (!e2) -> !e2
  %1 = "geomalg.sum"(%arg0, %arg1, %0) : (!e1, !e2, !e2) -> !vec2
  %2 = geomalg.call @widen_call_arg_callee(%1) : (!vec2) -> !vec2
  geomalg.return %2 : !vec2
}
