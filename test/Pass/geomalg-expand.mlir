// RUN: geomalg-opt \
// RUN:   --pass-pipeline="builtin.module(func.func(geomalg-expand))" %s \
// RUN:   | FileCheck %s

module {
// CHECK-LABEL: func.func @distribute_0()
// CHECK-NEXT: [[SUM1:%[0-9]+]] = "geomalg.sum"()
// CHECK-NEXT: geomalg.return [[SUM1]] : !geomalg.zero
func.func @distribute_0() -> !geomalg.zero {
  %1 = "geomalg.sum"() : () -> !geomalg.zero
  %2 = "geomalg.negate"(%1) : (!geomalg.zero) -> !geomalg.zero
  geomalg.return %2 : !geomalg.zero
}

// CHECK-LABEL: func.func @distribute_1
// CHECK-SAME: ([[ARG1:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[NEG1:%[0-9]+]] = "geomalg.negate"([[ARG1]])
// CHECK-NEXT: [[SUM1:%[0-9]+]]"geomalg.sum"([[NEG1]])
// CHECK-SAME: (!geomalg.blade<1>) -> !geomalg.blade<1>
// CHECK-NEXT: geomalg.return [[SUM1]] : !geomalg.blade<1>
func.func @distribute_1(%arg1: !geomalg.blade<1>) -> !geomalg.blade<1> {
  %1 = "geomalg.sum"(%arg1) : (!geomalg.blade<1>) -> !geomalg.blade<1>
  %2 = "geomalg.negate"(%1) : (!geomalg.blade<1>) -> !geomalg.blade<1>
  geomalg.return %2 : !geomalg.blade<1>
}

// CHECK-LABEL: func.func @distribute_2
// CHECK-SAME: ([[ARG1:%arg[0-9]+]]: !geomalg.blade<1>
// CHECK-SAME:  [[ARG2:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>>
// CHECK: [[NEG1:%[0-9]+]] = "geomalg.negate"([[ARG1]])
// CHECK: [[NEG2:%[0-9]+]] = "geomalg.negate"([[ARG2]])
// CHECK-NEXT: [[SUM1:%[0-9]+]]"geomalg.sum"([[NEG1]], [[NEG2]])
// CHECK-SAME: (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
// CHECK-NEXT: geomalg.return [[SUM1]] : !geomalg.multivector<<1>, <2>>
func.func @distribute_2(%arg1: !geomalg.blade<1>, %arg2: !geomalg.blade<2>)
                    -> !geomalg.multivector<<1>, <2>> {
  %1 = "geomalg.sum"(%arg1, %arg2)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %2 = "geomalg.negate"(%1)
    : (!geomalg.multivector<<1>, <2>>) -> !geomalg.multivector<<1>, <2>>
  geomalg.return %2 : !geomalg.multivector<<1>, <2>>
}

// Bilinear maps!

// Note we are not using a metric so iprod does not expand for 1-blades.
// CHECK-LABEL: func.func @distribute_b_0
// CHECK-SAME: ([[ARG1:%[0-9]+]]: !geomalg.blade<1>) -> !geomalg.zero
// CHECK-NEXT: [[SUM1:%[0-9]+]]"geomalg.sum"() -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[SUM1]] : !geomalg.zero
func.func @distribute_b_0(%arg1: !geomalg.blade<1>) -> !geomalg.unknown {
  %1 = "geomalg.sum"() : () -> !geomalg.zero
  %2 = "geomalg.iprod"(%1, %arg1)
    : (!geomalg.zero, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

// CHECK-LABEL: func.func @distribute_b_1
// CHECK-SAME: ([[ARG0:%[0-9]+]]: !geomalg.blade<1>) -> !geomalg.zero
// CHECK-NEXT: [[SUM1:%[0-9]+]]"geomalg.sum"() -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[SUM1]] : !geomalg.zero
func.func @distribute_b_1(%arg1: !geomalg.blade<1>) -> !geomalg.unknown {
  %1 = "geomalg.sum"() : () -> !geomalg.zero
  %2 = "geomalg.iprod"(%arg1, %1)
    : (!geomalg.blade<1>, !geomalg.zero) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

func.func @distribute_b_2(%arg1: !geomalg.blade<1>, %arg2: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %1 = "geomalg.sum"(%arg1, %arg2)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %2 = "geomalg.iprod"(%arg1, %1)
    : (!geomalg.blade<1>, !geomalg.multivector<<1>, <2>>) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

func.func @distribute_b_3(%arg1: !geomalg.blade<1>, %arg2: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %1 = "geomalg.sum"(%arg1, %arg2)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %2 = "geomalg.iprod"(%1, %arg1)
    : (!geomalg.multivector<<1>, <2>>, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

func.func @distribute_b_4(%arg1: !geomalg.blade<1>, %arg2: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %1 = "geomalg.sum"(%arg1, %arg2)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %2 = "geomalg.iprod"(%1, %1)
    : (!geomalg.multivector<<1>, <2>>,
       !geomalg.multivector<<1>, <2>>)
      -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

}  // module
