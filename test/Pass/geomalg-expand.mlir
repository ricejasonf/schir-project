// RUN: geomalg-opt \
// RUN:   --pass-pipeline="builtin.module(func.func(geomalg-expand))" %s \
// RUN:   | FileCheck %s

module {
// CHECK-LABEL: func.func @sum_0()
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"()
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.zero
func.func @sum_0() -> !geomalg.zero {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.sum"(%0, %0) : (!geomalg.zero, !geomalg.zero) -> !geomalg.zero
  geomalg.return %1 : !geomalg.zero
}

// CHECK-LABEL: func.func @sum_1
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NEXT: geomalg.return [[ARG0]] : !geomalg.blade<1>
func.func @sum_1(%arg0: !geomalg.blade<1>) -> !geomalg.blade<1> {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.sum"(%arg0, %0) : (!geomalg.blade<1>, !geomalg.zero)
    -> !geomalg.blade<1>
  geomalg.return %1 : !geomalg.blade<1>
}

// CHECK-LABEL: func.func @sum_2
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NEXT: geomalg.return [[ARG0]] : !geomalg.blade<1>
func.func @sum_2(%arg0: !geomalg.blade<1>) -> !geomalg.blade<1> {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.sum"(%0, %arg0) : (!geomalg.zero, !geomalg.blade<1>)
    -> !geomalg.blade<1>
  geomalg.return %1 : !geomalg.blade<1>
}
// CHECK-LABEL: func.func @sum_3
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[ARG0]], [[ARG0]])
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.blade<1>
func.func @sum_3(%arg0: !geomalg.blade<1>) -> !geomalg.blade<1> {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.sum"(%arg0, %0, %arg0)
    : (!geomalg.blade<1>, !geomalg.zero, !geomalg.blade<1>)
      -> !geomalg.blade<1>
  geomalg.return %1 : !geomalg.blade<1>
}

// CHECK-LABEL: func.func @distribute_0()
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"()
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.zero
func.func @distribute_0() -> !geomalg.zero {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.negate"(%0) : (!geomalg.zero) -> !geomalg.zero
  geomalg.return %1 : !geomalg.zero
}

// CHECK-LABEL: func.func @distribute_1
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[NEG0:%[0-9]+]] = "geomalg.negate"([[ARG0]])
// CHECK-SAME: (!geomalg.blade<1>) -> !geomalg.blade<1>
// CHECK-NEXT: geomalg.return [[NEG0]] : !geomalg.blade<1>
func.func @distribute_1(%arg0: !geomalg.blade<1>) -> !geomalg.blade<1> {
  %0 = "geomalg.sum"(%arg0) : (!geomalg.blade<1>) -> !geomalg.blade<1>
  %1 = "geomalg.negate"(%0) : (!geomalg.blade<1>) -> !geomalg.blade<1>
  geomalg.return %1 : !geomalg.blade<1>
}

// CHECK-LABEL: func.func @distribute_2
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>
// CHECK-SAME:  [[ARG1:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>>
// CHECK: [[NEG0:%[0-9]+]] = "geomalg.negate"([[ARG0]])
// CHECK: [[NEG1:%[0-9]+]] = "geomalg.negate"([[ARG1]])
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[NEG0]], [[NEG1]])
// CHECK-SAME: (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.multivector<<1>, <2>>
func.func @distribute_2(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>)
                    -> !geomalg.multivector<<1>, <2>> {
  %0 = "geomalg.sum"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %1 = "geomalg.negate"(%0)
    : (!geomalg.multivector<<1>, <2>>) -> !geomalg.multivector<<1>, <2>>
  geomalg.return %1 : !geomalg.multivector<<1>, <2>>
}

// Bilinear maps!

// Note we are not using a metric so iprod does not expand for 1-blades.
// CHECK-LABEL: func.func @distribute_b_0
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"() : () -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.zero
func.func @distribute_b_0(%arg0: !geomalg.blade<1>) -> !geomalg.unknown {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.iprod"(%0, %arg0)
    : (!geomalg.zero, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// CHECK-LABEL: func.func @distribute_b_1
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"() : () -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.zero
func.func @distribute_b_1(%arg0: !geomalg.blade<1>) -> !geomalg.unknown {
  %0 = "geomalg.sum"() : () -> !geomalg.zero
  %1 = "geomalg.iprod"(%arg0, %0)
    : (!geomalg.blade<1>, !geomalg.zero) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// CHECK-LABEL: func.func @distribute_b_2
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[ARG1:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK-NEXT: [[IPR0:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG0]])
// CHECK-SAME:  -> !geomalg.unknown
// CHECK-NEXT: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG1]])
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]])
// CHECK-SAME:  -> !geomalg.unknown
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.unknown
func.func @distribute_b_2(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %0 = "geomalg.sum"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %1 = "geomalg.iprod"(%arg0, %0)
    : (!geomalg.blade<1>, !geomalg.multivector<<1>, <2>>) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// CHECK-LABEL: func.func @distribute_b_3
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[ARG1:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK-NEXT: [[IPR0:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG0]])
// CHECK-SAME:  -> !geomalg.unknown
// CHECK-NEXT: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG0]])
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]])
// CHECK-SAME:  -> !geomalg.unknown
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.unknown
func.func @distribute_b_3(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %0 = "geomalg.sum"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %1 = "geomalg.iprod"(%0, %arg0)
    : (!geomalg.multivector<<1>, <2>>, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

// CHECK-LABEL: func.func @distribute_b_4
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[ARG1:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK-NEXT: [[IPR0:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG0]])
// CHECK-NEXT: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG1]])
// CHECK-NEXT: [[IPR2:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG0]])
// CHECK-NEXT: [[IPR3:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG1]])
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]],
// CHECK-SAME:                                  [[IPR2]], [[IPR3]])
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.unknown
func.func @distribute_b_4(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>)
                          -> !geomalg.unknown {
  %0 = "geomalg.sum"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.multivector<<1>, <2>>
  %1 = "geomalg.iprod"(%0, %0)
    : (!geomalg.multivector<<1>, <2>>,
       !geomalg.multivector<<1>, <2>>)
      -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}

}  // module
