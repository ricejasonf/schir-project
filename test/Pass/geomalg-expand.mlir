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
// CHECK-SAME:  -> !geomalg.blade<0>
// CHECK-NEXT: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG1]])
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]])
// CHECK-SAME:  -> !geomalg.blade<0>
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.blade<0>
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
// CHECK-SAME:  -> !geomalg.blade<0>
// CHECK-NEXT: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG0]])
// CHECK-SAME:  -> !geomalg.blade<0>
// CHECK-NEXT: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]])
// CHECK-SAME:  -> !geomalg.blade<0>
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.blade<0>
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
// CHECK: [[IPR0:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG0]])
// CHECK: [[IPR1:%[0-9]+]] = "geomalg.iprod"([[ARG0]], [[ARG1]])
// CHECK: [[IPR2:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG0]])
// CHECK: [[IPR3:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[ARG1]])
// CHECK: [[SUM0:%[0-9]+]] = "geomalg.sum"([[IPR0]], [[IPR1]],
// CHECK-SAME:                             [[IPR2]], [[IPR3]])
// CHECK-NEXT: geomalg.return [[SUM0]] : !geomalg.blade<0>
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

// 3.7
// α ⌋ B = α B
// CHECK-LABEL: func.func @iprod_3_7
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<0>,
// CHECK-SAME:  [[B:%arg[0-9]+]]: !geomalg.blade<3>)
// CHECK: [[aB:%[0-9]+]] = "geomalg.iprod"([[a]], [[B]])
// CHECK-SAME: -> !geomalg.blade<3>
// CHECK-NEXT: geomalg.return [[aB]]
func.func @iprod_3_7(%arg0: !geomalg.blade<0>, %arg1: !geomalg.blade<3>)
                          -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%arg0, %arg1)
    : (!geomalg.blade<0>, !geomalg.blade<3>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// 3.8
// B ⌋ α = 0
// CHECK-LABEL: func.func @iprod_3_8
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<0>,
// CHECK-SAME:  [[B:%arg[0-9]+]]: !geomalg.blade<3>)
// CHECK: [[aB:%[0-9]+]] = "geomalg.iprod"([[B]], [[a]])
// CHECK-SAME: -> !geomalg.zero
// CHECK-NEXT: geomalg.return [[aB]]
func.func @iprod_3_8(%arg0: !geomalg.blade<0>, %arg1: !geomalg.blade<3>)
                          -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%arg1, %arg0)
    : (!geomalg.blade<3>, !geomalg.blade<0>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// 3.10
// a ⌋ (b ∧ C) = ((a ⌋ b) ∧ C) + ((a ⌋ C) ∧ b)
// Note we used the antisymmetric property for the second term
// CHECK-LABEL: func.func @iprod_3_10
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[bC:%arg[0-9]+]]: !geomalg.blade<3>)
// CHECK: [[b:%[0-9]+]] = "geomalg.blade"()
// CHECK-SAME:   coefficient = 1
// CHECK: [[C:%[0-9]+]] = "geomalg.cast"([[bC]])
// CHECK-SAME: : (!geomalg.blade<3>) -> !geomalg.blade<2>
// CHECK: [[ab:%[0-9]+]] = "geomalg.iprod"([[a]], [[b]])
// CHECK: [[aC:%[0-9]+]] = "geomalg.iprod"([[a]], [[C]])
// CHECK: [[ab_C:%[0-9]+]] = "geomalg.oprod"([[ab]], [[C]])
// CHECK: [[aC_b:%[0-9]+]] = "geomalg.oprod"([[aC]], [[b]])
// CHECK: [[SUM0:%[0-9]+]] = "geomalg.sum"([[aC_b]], [[ab_C]])
// CHECK-NEXT: geomalg.return [[SUM0]]
func.func @iprod_3_10(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<3>)
                          -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<3>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// 3.11
// (a ∧ B) ⌋ C = a ⌋ (B ⌋ C)
// Note we can use a 1-blade for C since we do not have a metric.
// CHECK-LABEL: func.func @iprod_3_11
// CHECK-SAME: ([[aB:%arg[0-9]+]]: !geomalg.blade<6>,
// CHECK-SAME:  [[C:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[a:%[0-9]+]] = "geomalg.blade"()
// CHECK-SAME:   coefficient = 1
// CHECK: [[B:%[0-9]+]] = "geomalg.cast"([[aB]])
// CHECK-SAME: : (!geomalg.blade<6>) -> !geomalg.blade<4>
// CHECK: [[BC:%[0-9]+]] = "geomalg.iprod"([[B]], [[C]])
// CHECK: [[aBC:%[0-9]+]] = "geomalg.iprod"([[a]], [[BC]])
// CHECK-NEXT: geomalg.return [[aBC]]
func.func @iprod_3_11(%arg0: !geomalg.blade<6>, %arg1: !geomalg.blade<1>)
                          -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%arg0, %arg1)
    : (!geomalg.blade<6>, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// α a = α ⌋ a
// CHECK-LABEL: @gprod_0
// CHECK-SAME: ([[alpha:%arg[0-9]+]]: !geomalg.blade<0>,
// CHECK-SAME:  [[a:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[RESULT:%[0-9]+]] = "geomalg.iprod"([[alpha]], [[a]])
// CHECK: geomalg.return [[RESULT]]
func.func @gprod_0(%arg0: !geomalg.blade<0>, %arg1: !geomalg.blade<1>)
            -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%arg0, %arg1)
    : (!geomalg.blade<0>, !geomalg.blade<1>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}
// a α = a ⌋ α
// CHECK-LABEL: @gprod_1
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[alpha:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK: [[RESULT:%[0-9]+]] = "geomalg.iprod"([[alpha]], [[a]])
// CHECK: geomalg.return [[RESULT]]
func.func @gprod_1(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<0>)
            -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<0>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// a B = a ⌋ B + a ∧ B
// CHECK-LABEL: @gprod_2
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<1>,
// CHECK-SAME:  [[B:%arg[0-9]+]]: !geomalg.blade<2>)
// CHECK: [[aB:%[0-9]+]] = "geomalg.iprod"([[a]], [[B]])
// CHECK: [[a_B:%[0-9]+]] = "geomalg.oprod"([[a]], [[B]])
// CHECK-SAME: -> !geomalg.blade<3>
// CHECK: [[SUM0:%[0-9]+]] = "geomalg.sum"([[aB]], [[a_B]])
// CHECK: geomalg.return [[SUM0]]
func.func @gprod_2(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>)
            -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%arg0, %arg1)
    : (!geomalg.blade<1>, !geomalg.blade<2>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// (α)⁻¹ = 1 / α
// CHECK-LABEL: @inverse_k0
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK: [[INV_a:%[0-9]+]] = "geomalg.inverse"([[a]])
// CHECK-SAME:(!geomalg.blade<0>) -> !geomalg.blade<0>
// CHECK: geomalg.return [[INV_a]]
func.func @inverse_k0(%arg0: !geomalg.blade<0>)
            -> !geomalg.blade<0> {
  %0 = "geomalg.inverse"(%arg0)
    : (!geomalg.blade<0>) -> !geomalg.blade<0>
  geomalg.return %0 : !geomalg.blade<0>
}

// (a)⁻¹ = (a ⌋ a)⁻¹ (-1)^{k (k - 1) / 2} a
// For k-blade and k = 1,
// CHECK-LABEL: @inverse_k1
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[MAG_a:%[0-9]+]] = "geomalg.iprod"([[a]], [[a]])
// CHECK-SAME: -> !geomalg.blade<0>
// CHECK: [[INV_MAG_a:%[0-9]+]] = "geomalg.inverse"([[MAG_a]])
// CHECK: [[PROD_a:%[0-9]+]] = "geomalg.iprod"([[INV_MAG_a]], [[a]])
// CHECK-SAME: -> !geomalg.blade<1>
// CHECK: geomalg.return [[PROD_a]]
func.func @inverse_k1(%arg0: !geomalg.blade<1>)
            -> !geomalg.blade<1> {
  %0 = "geomalg.inverse"(%arg0)
    : (!geomalg.blade<1>) -> !geomalg.blade<1>
  geomalg.return %0 : !geomalg.blade<1>
}

// (a)⁻¹ = (a ⌋ a)⁻¹ (-1)^{k (k - 1) / 2} a
// For k-blade and k = 2,
// TODO Test expansion of the blade with iprod or make it not expand at all
//      for unknown metric.
// CHECK-LABEL: @inverse_k2
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.blade<3>)
// COM-CHECK: [[NEG_a:%[0-9]+]] = "geomalg.negate"([[a]])
// COM-CHECK: [[MAG_a:%[0-9]+]] = "geomalg.iprod"([[a]], [[a]])
// COM-CHECK-SAME: -> !geomalg.blade<0>
// COM-CHECK: [[INV_MAG_a:%[0-9]+]] = "geomalg.inverse"([[MAG_a]])
// COM-CHECK: [[PROD_a:%[0-9]+]] = "geomalg.iprod"([[INV_MAG_a]], [[NEG_a]])
// COM-CHECK-SAME: -> !geomalg.blade<3>
// COM-CHECK: geomalg.return [[PROD_a]]
func.func @inverse_k2(%arg0: !geomalg.blade<3>) -> !geomalg.blade<3> {
  %0 = "geomalg.inverse"(%arg0)
    : (!geomalg.blade<3>) -> !geomalg.blade<3>
  geomalg.return %0 : !geomalg.blade<3>
}

// (a)⁻¹ = (a ⌋ a)⁻¹ a
// For 1-vector,
// CHECK-LABEL: @inverse_v1
// CHECK-SAME: ([[a:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>)
// CHECK: [[EX:%[0-9]+]]:3 = "geomalg.expand"([[a]])
// CHECK: [[I1:%[0-9]+]] = "geomalg.iprod"([[EX]]#0, [[EX]]#0)
// CHECK: [[I2:%[0-9]+]] = "geomalg.iprod"([[EX]]#0, [[EX]]#1)
// CHECK: [[I3:%[0-9]+]] = "geomalg.iprod"([[EX]]#0, [[EX]]#2)
// CHECK: [[I4:%[0-9]+]] = "geomalg.iprod"([[EX]]#1, [[EX]]#0)
// CHECK: [[I5:%[0-9]+]] = "geomalg.iprod"([[EX]]#1, [[EX]]#1)
// CHECK: [[I6:%[0-9]+]] = "geomalg.iprod"([[EX]]#1, [[EX]]#2)
// CHECK: [[I7:%[0-9]+]] = "geomalg.iprod"([[EX]]#2, [[EX]]#0)
// CHECK: [[I8:%[0-9]+]] = "geomalg.iprod"([[EX]]#2, [[EX]]#1)
// CHECK: [[I9:%[0-9]+]] = "geomalg.iprod"([[EX]]#2, [[EX]]#2)
// CHECK: [[MAG_a:[%0-9]+]] = "geomalg.sum"(
// CHECK-SAME: [[I1]], [[I2]], [[I3]], [[I4]], [[I5]],
// CHECK-SAME: [[I6]], [[I7]], [[I8]], [[I9]])
// CHECK: [[INV_MAG_a:%[0-9]+]] = "geomalg.inverse"([[MAG_a]])
// CHECK: [[INV_a1:%[0-9]+]] = "geomalg.iprod"([[INV_MAG_a]], [[EX]]#0)
// CHECK: [[INV_a2:%[0-9]+]] = "geomalg.iprod"([[INV_MAG_a]], [[EX]]#1)
// CHECK: [[INV_a3:%[0-9]+]] = "geomalg.iprod"([[INV_MAG_a]], [[EX]]#2)
// CHECK: [[RESULT:%[0-9]+]] = "geomalg.sum"(
// CHECK-SAME: [[INV_a1]], [[INV_a2]], [[INV_a3]])
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>>
// CHECK: geomalg.return [[RESULT]]
func.func @inverse_v1(%arg0: !geomalg.multivector<<1>, <2>, <4>>)
            -> !geomalg.multivector<<1>, <2>, <4>> {
  %0 = "geomalg.inverse"(%arg0)
    : (!geomalg.multivector<<1>, <2>, <4>>)
      -> !geomalg.multivector<<1>, <2>, <4>>
  geomalg.return %0 : !geomalg.multivector<<1>, <2>, <4>>
}
}  // module
