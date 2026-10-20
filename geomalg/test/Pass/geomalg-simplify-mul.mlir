// RUN: geomalg-opt --geomalg-simplify %s | FileCheck %s

!scalar = !geomalg.blade<0>

// Constant factors that are not powers of two fold into one coefficient,
// rounded to f32 once.
// CHECK-LABEL: func.func @fold_constants
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK: [[C:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0300000012 : f32}>
// CHECK: "geomalg.cmul"([[ARG0]], [[C]])
// CHECK-NOT: "geomalg.cmul"
func.func @fold_constants(%arg0: !scalar) -> !scalar {
  %0 = "geomalg.blade"() <{coefficient = 1.000000e-01 : f32}> : () -> !scalar
  %1 = "geomalg.blade"() <{coefficient = 3.000000e-01 : f32}> : () -> !scalar
  %2 = "geomalg.cmul"(%0, %arg0) : (!scalar, !scalar) -> !scalar
  %3 = "geomalg.cmul"(%2, %1) : (!scalar, !scalar) -> !scalar
  geomalg.return %3 : !scalar
}

// A negative constant folds its sign into a negate.
// CHECK-LABEL: func.func @fold_negative
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK: [[C:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0300000012 : f32}>
// CHECK: [[M:%[0-9]+]] = "geomalg.cmul"([[ARG0]], [[C]])
// CHECK: "geomalg.negate"([[M]])
func.func @fold_negative(%arg0: !scalar) -> !scalar {
  %0 = "geomalg.blade"() <{coefficient = -3.000000e-01 : f32}> : () -> !scalar
  %1 = "geomalg.blade"() <{coefficient = 1.000000e-01 : f32}> : () -> !scalar
  %2 = "geomalg.cmul"(%0, %arg0, %1) : (!scalar, !scalar, !scalar) -> !scalar
  geomalg.return %2 : !scalar
}
