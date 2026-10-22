// RUN: geomalg-opt --geomalg-factor %s | FileCheck %s
// RUN: geomalg-opt --geomalg-lower %s | FileCheck --check-prefix=LOWER %s

!scalar = !geomalg.blade<0>
!e1 = !geomalg.blade<1>
!e2 = !geomalg.blade<2>
!e12 = !geomalg.blade<3>

// Multiply a factor shared by terms once.
// CHECK-LABEL: func.func @common_factor
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[D:%arg[0-9]+]]: {{.*}})
// CHECK-DAG: [[MB:%[0-9]+]] = "geomalg.cmul"([[B]])
// CHECK-DAG: [[MC:%[0-9]+]] = "geomalg.cmul"([[C]])
// CHECK: [[S:%[0-9]+]] = "geomalg.sum"([[MB]], [[MC]]) {{.*}} -> !geomalg.blade<0>
// CHECK: [[P:%[0-9]+]] = "geomalg.cmul"([[A]], [[S]]) {{.*}} -> !geomalg.blade<3>
// CHECK: [[R:%[0-9]+]] = "geomalg.sum"([[P]], [[D]])
// CHECK: geomalg.return [[R]]
func.func @common_factor(%a: !e1, %b: !e2, %c: !e2, %d: !e12) -> !e12 {
  %0 = "geomalg.cmul"(%a, %b) : (!e1, !e2) -> !e12
  %1 = "geomalg.cmul"(%a, %c) : (!e1, !e2) -> !e12
  %2 = "geomalg.sum"(%0, %d, %1) : (!e12, !e12, !e12) -> !e12
  geomalg.return %2 : !e12
}

// Factor 2ab - 2cd into 2(ab - cd).
// CHECK-LABEL: func.func @common_constant
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[D:%arg[0-9]+]]: {{.*}})
// CHECK-DAG: [[AB:%[0-9]+]] = "geomalg.cmul"([[A]], [[B]])
// CHECK-DAG: [[CD:%[0-9]+]] = "geomalg.cmul"([[C]], [[D]])
// CHECK-DAG: [[NCD:%[0-9]+]] = "geomalg.negate"([[CD]])
// CHECK-DAG: [[TWO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 2.0{{.*}}}>
// CHECK: [[S:%[0-9]+]] = "geomalg.sum"([[AB]], [[NCD]])
// CHECK: [[R:%[0-9]+]] = "geomalg.cmul"([[TWO]], [[S]])
// CHECK: geomalg.return [[R]]
func.func @common_constant(%a: !scalar, %b: !scalar,
                           %c: !scalar, %d: !scalar) -> !scalar {
  %two = "geomalg.blade"() <{coefficient = 2.0 : f32}> : () -> !scalar
  %0 = "geomalg.cmul"(%a, %b, %two) : (!scalar, !scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%c, %d, %two) : (!scalar, !scalar, !scalar) -> !scalar
  %2 = "geomalg.negate"(%1) : (!scalar) -> !scalar
  %3 = "geomalg.sum"(%0, %2) : (!scalar, !scalar) -> !scalar
  geomalg.return %3 : !scalar
}

// Do not factor out a zero coefficient.
// CHECK-LABEL: func.func @zero_constant
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}})
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK: [[MA:%[0-9]+]] = "geomalg.cmul"([[A]], [[ZERO]])
// CHECK: [[MB:%[0-9]+]] = "geomalg.cmul"([[B]], [[ZERO]])
// CHECK: [[R:%[0-9]+]] = "geomalg.sum"([[MA]], [[MB]])
// CHECK: geomalg.return [[R]]
func.func @zero_constant(%a: !scalar, %b: !scalar) -> !scalar {
  %zero = "geomalg.blade"() <{coefficient = 0.0 : f32}> : () -> !scalar
  %0 = "geomalg.cmul"(%a, %zero) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%b, %zero) : (!scalar, !scalar) -> !scalar
  %2 = "geomalg.sum"(%0, %1) : (!scalar, !scalar) -> !scalar
  geomalg.return %2 : !scalar
}

// Leave ab + a unchanged since a(b + 1) has as many multiplications.
// CHECK-LABEL: func.func @no_savings
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}})
// CHECK: [[AB:%[0-9]+]] = "geomalg.cmul"([[A]], [[B]])
// CHECK: [[R:%[0-9]+]] = "geomalg.sum"([[AB]], [[A]])
// CHECK: geomalg.return [[R]]
func.func @no_savings(%a: !scalar, %b: !scalar) -> !scalar {
  %0 = "geomalg.cmul"(%a, %b) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.sum"(%0, %a) : (!scalar, !scalar) -> !scalar
  geomalg.return %1 : !scalar
}

// Multiply a pair of factors shared by products once.
// CHECK-LABEL: func.func @shared_pair
// CHECK-SAME: ([[X:%arg[0-9]+]]: {{.*}}, [[Y:%arg[0-9]+]]: {{.*}}, [[S:%arg[0-9]+]]: {{.*}}, [[T:%arg[0-9]+]]: {{.*}})
// CHECK: [[ST:%[0-9]+]] = "geomalg.cmul"([[S]], [[T]]) {{.*}} -> !geomalg.blade<0>
// CHECK: "geomalg.cmul"([[ST]], [[X]]) {{.*}} -> !geomalg.blade<1>
// CHECK: "geomalg.cmul"([[ST]], [[Y]]) {{.*}} -> !geomalg.blade<2>
// LOWER-LABEL: func.func @shared_pair
// LOWER-COUNT-3: arith.mulf
// LOWER-NOT: arith.mulf
// LOWER: return
func.func @shared_pair(%x: !e1, %y: !e2, %s: !scalar, %t: !scalar)
    -> !geomalg.multivector<<1>, <2>> {
  %0 = "geomalg.cmul"(%x, %s, %t) : (!e1, !scalar, !scalar) -> !e1
  %1 = "geomalg.cmul"(%y, %s, %t) : (!e2, !scalar, !scalar) -> !e2
  %2 = "geomalg.sum"(%0, %1) : (!e1, !e2) -> !geomalg.multivector<<1>, <2>>
  geomalg.return %2 : !geomalg.multivector<<1>, <2>>
}
