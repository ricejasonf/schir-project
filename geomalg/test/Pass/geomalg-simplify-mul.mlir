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

// Fold a product with a zero constant to zero.
// CHECK-LABEL: func.func @fold_zero_cmul
// CHECK: [[Z:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}> : () -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[Z]] : !geomalg.zero
func.func @fold_zero_cmul(%arg0: !scalar) -> !geomalg.unknown {
  %0 = "geomalg.blade"() <{coefficient = 0.000000e+00 : f32}> : () -> !scalar
  %1 = "geomalg.cmul"(%0, %arg0) : (!scalar, !scalar) -> !scalar
  geomalg.return %1 : !scalar
}

// Drop a product with a zero constant from a sum.
// CHECK-LABEL: func.func @fold_zero_iprod
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>, [[ARG1:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK-NEXT: geomalg.return [[ARG1]] : !geomalg.blade<0>
func.func @fold_zero_iprod(%arg0: !geomalg.blade<1>, %arg1: !scalar)
    -> !geomalg.unknown {
  %0 = "geomalg.blade"() <{coefficient = 0.000000e+00 : f32}> : () -> !scalar
  %1 = "geomalg.iprod"(%0, %arg0) : (!scalar, !geomalg.blade<1>) -> !geomalg.unknown
  %2 = "geomalg.sum"(%1, %arg1) : (!geomalg.unknown, !scalar) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

// Cancel a scalar with its inverse in a product.
// CHECK-LABEL: func.func @cancel_inverse
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<0>, [[ARG1:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NOT: "geomalg.cmul"
// CHECK: geomalg.return [[ARG1]] : !geomalg.blade<1>
func.func @cancel_inverse(%arg0: !scalar, %arg1: !geomalg.blade<1>)
    -> !geomalg.blade<1> {
  %0 = "geomalg.inverse"(%arg0) : (!scalar) -> !scalar
  %1 = "geomalg.cmul"(%arg0, %arg1, %0) : (!scalar, !geomalg.blade<1>, !scalar) -> !geomalg.blade<1>
  geomalg.return %1 : !geomalg.blade<1>
}

// Cancel a negated scalar with its inverse and keep the sign.
// CHECK-LABEL: func.func @cancel_negated_inverse
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<0>, [[ARG1:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK-NOT: "geomalg.inverse"
// CHECK: [[N:%[0-9]+]] = "geomalg.negate"([[ARG1]])
// CHECK-NEXT: geomalg.return [[N]] : !geomalg.blade<0>
func.func @cancel_negated_inverse(%arg0: !scalar, %arg1: !scalar) -> !scalar {
  %0 = "geomalg.negate"(%arg0) : (!scalar) -> !scalar
  %1 = "geomalg.inverse"(%0) : (!scalar) -> !scalar
  %2 = "geomalg.cmul"(%arg0, %1, %arg1) : (!scalar, !scalar, !scalar) -> !scalar
  geomalg.return %2 : !scalar
}

// Keep the inverse of a scalar that is not a factor.
// CHECK-LABEL: func.func @keep_inverse
// CHECK: [[I:%[0-9]+]] = "geomalg.inverse"(%arg0)
// CHECK: "geomalg.cmul"(%arg1, [[I]])
func.func @keep_inverse(%arg0: !scalar, %arg1: !scalar) -> !scalar {
  %0 = "geomalg.inverse"(%arg0) : (!scalar) -> !scalar
  %1 = "geomalg.cmul"(%arg1, %0) : (!scalar, !scalar) -> !scalar
  geomalg.return %1 : !scalar
}
