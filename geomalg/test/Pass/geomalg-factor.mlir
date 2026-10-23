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

// Factor all of the factors shared by the terms at once.
// CHECK-LABEL: func.func @common_cube
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[D:%arg[0-9]+]]: {{.*}})
// CHECK: [[S:%[0-9]+]] = "geomalg.sum"([[C]], [[D]])
// CHECK: [[R:%[0-9]+]] = "geomalg.cmul"([[A]], [[B]], [[S]])
// CHECK: geomalg.return [[R]]
func.func @common_cube(%a: !scalar, %b: !scalar,
                       %c: !scalar, %d: !scalar) -> !scalar {
  %0 = "geomalg.cmul"(%a, %b, %c) : (!scalar, !scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%a, %b, %d) : (!scalar, !scalar, !scalar) -> !scalar
  %2 = "geomalg.sum"(%0, %1) : (!scalar, !scalar) -> !scalar
  geomalg.return %2 : !scalar
}

// Factor ab + ac + bd + ce into b(a + d) + c(a + e) rather than
// a(b + c) + bd + ce.
// CHECK-LABEL: func.func @lookahead
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[D:%arg[0-9]+]]: {{.*}}, [[E:%arg[0-9]+]]: {{.*}})
// CHECK-DAG: "geomalg.cmul"([[B]], {{%[0-9]+}})
// CHECK-DAG: "geomalg.cmul"([[C]], {{%[0-9]+}})
// CHECK-NOT: "geomalg.cmul"([[A]], {{%[0-9]+}})
// CHECK: geomalg.return
// LOWER-LABEL: func.func @lookahead
// LOWER-COUNT-2: arith.mulf
// LOWER-NOT: arith.mulf
// LOWER: return
func.func @lookahead(%a: !scalar, %b: !scalar, %c: !scalar,
                     %d: !scalar, %e: !scalar) -> !scalar {
  %0 = "geomalg.cmul"(%a, %b) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%a, %c) : (!scalar, !scalar) -> !scalar
  %2 = "geomalg.cmul"(%b, %d) : (!scalar, !scalar) -> !scalar
  %3 = "geomalg.cmul"(%c, %e) : (!scalar, !scalar) -> !scalar
  %4 = "geomalg.sum"(%0, %1, %2, %3) : (!scalar, !scalar, !scalar, !scalar) -> !scalar
  geomalg.return %4 : !scalar
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

// Complete the square ½x² - xc + ½c² as ½(x - c)².
// CHECK-LABEL: func.func @complete_square
// CHECK-SAME: ([[X:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}})
// CHECK: [[NC:%[0-9]+]] = "geomalg.negate"([[C]])
// CHECK: [[D:%[0-9]+]] = "geomalg.sum"([[X]], [[NC]])
// CHECK: [[HALF:%[0-9]+]] = "geomalg.blade"() <{coefficient = 5.0{{.*}}e-01
// CHECK: [[R:%[0-9]+]] = "geomalg.cmul"([[D]], [[D]], [[HALF]])
// CHECK: geomalg.return [[R]]
// LOWER-LABEL: func.func @complete_square
// LOWER-COUNT-2: arith.mulf
// LOWER-NOT: arith.mulf
// LOWER: return
func.func @complete_square(%x: !scalar, %c: !scalar) -> !scalar {
  %half = "geomalg.blade"() <{coefficient = 0.5 : f32}> : () -> !scalar
  %0 = "geomalg.cmul"(%half, %x, %x) : (!scalar, !scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%x, %c) : (!scalar, !scalar) -> !scalar
  %2 = "geomalg.negate"(%1) : (!scalar) -> !scalar
  %3 = "geomalg.cmul"(%half, %c, %c) : (!scalar, !scalar, !scalar) -> !scalar
  %4 = "geomalg.sum"(%0, %2, %3) : (!scalar, !scalar, !scalar) -> !scalar
  geomalg.return %4 : !scalar
}

// Cancel the components of a dot product with the like terms.
// CHECK-LABEL: func.func @cancel_dot
// CHECK-SAME: ([[V:%arg[0-9]+]]: {{.*}}, [[A:%arg[0-9]+]]: {{.*}})
// CHECK-NEXT: geomalg.return [[A]]
func.func @cancel_dot(%v: !geomalg.multivector<<1>, <2>>, %a: !scalar)
    -> !scalar {
  %0:2 = "geomalg.expand"(%v) : (!geomalg.multivector<<1>, <2>>) -> (!e1, !e2)
  %1 = "geomalg.dot"(%v, %v) : (!geomalg.multivector<<1>, <2>>,
                                !geomalg.multivector<<1>, <2>>) -> !scalar
  %2 = "geomalg.cmul"(%0#0, %0#0) : (!e1, !e1) -> !scalar
  %3 = "geomalg.negate"(%2) : (!scalar) -> !scalar
  %4 = "geomalg.cmul"(%0#1, %0#1) : (!e2, !e2) -> !scalar
  %5 = "geomalg.negate"(%4) : (!scalar) -> !scalar
  %6 = "geomalg.sum"(%1, %3, %5, %a) : (!scalar, !scalar, !scalar, !scalar) -> !scalar
  geomalg.return %6 : !scalar
}

// Cancel a factor with the inverse of a sum that reduces to it.
// CHECK-LABEL: func.func @cancel_inverse
// CHECK-SAME: ([[R:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[X:%arg[0-9]+]]: {{.*}}, [[Y:%arg[0-9]+]]: {{.*}})
// CHECK-NEXT: [[S:%[0-9]+]] = "geomalg.sum"([[X]], [[Y]])
// CHECK-NEXT: geomalg.return [[S]]
func.func @cancel_inverse(%r: !scalar, %c: !scalar,
                          %x: !scalar, %y: !scalar) -> !scalar {
  %0 = "geomalg.cmul"(%r, %r) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%c, %c) : (!scalar, !scalar) -> !scalar
  %2 = "geomalg.negate"(%1) : (!scalar) -> !scalar
  %3 = "geomalg.sum"(%0, %1, %2) : (!scalar, !scalar, !scalar) -> !scalar
  %4 = "geomalg.inverse"(%3) : (!scalar) -> !scalar
  %5 = "geomalg.cmul"(%4, %r, %r, %x) : (!scalar, !scalar, !scalar, !scalar) -> !scalar
  %6 = "geomalg.sum"(%5, %y) : (!scalar, !scalar) -> !scalar
  geomalg.return %6 : !scalar
}

// Do not distribute the sums a - b and a + b in (a - b)x - ½(a - b)(a + b).
// CHECK-LABEL: func.func @keep_difference
// CHECK-SAME: ([[X:%arg[0-9]+]]: {{.*}}, [[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}})
// CHECK-DAG: [[NB:%[0-9]+]] = "geomalg.negate"([[B]])
// CHECK-DAG: [[N:%[0-9]+]] = "geomalg.sum"([[A]], [[NB]])
// CHECK-DAG: [[S:%[0-9]+]] = "geomalg.sum"([[A]], [[B]])
// CHECK-DAG: "geomalg.cmul"([[S]], {{%[0-9]+}})
// CHECK: [[R:%[0-9]+]] = "geomalg.cmul"([[N]], {{%[0-9]+}})
// CHECK: geomalg.return [[R]]
func.func @keep_difference(%x: !scalar, %a: !scalar, %b: !scalar) -> !scalar {
  %half = "geomalg.blade"() <{coefficient = 0.5 : f32}> : () -> !scalar
  %nb = "geomalg.negate"(%b) : (!scalar) -> !scalar
  %n = "geomalg.sum"(%a, %nb) : (!scalar, !scalar) -> !scalar
  %s = "geomalg.sum"(%a, %b) : (!scalar, !scalar) -> !scalar
  %0 = "geomalg.cmul"(%n, %x) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%half, %n, %s) : (!scalar, !scalar, !scalar) -> !scalar
  %2 = "geomalg.negate"(%1) : (!scalar) -> !scalar
  %3 = "geomalg.sum"(%0, %2) : (!scalar, !scalar) -> !scalar
  geomalg.return %3 : !scalar
}

// Do not distribute (a + bc)(d + ef) into more terms than the sum has
// even though g² - g² cancels.
// CHECK-LABEL: func.func @no_growth
// CHECK-SAME: ([[A:%arg[0-9]+]]: {{.*}}, [[B:%arg[0-9]+]]: {{.*}}, [[C:%arg[0-9]+]]: {{.*}}, [[D:%arg[0-9]+]]: {{.*}}, [[E:%arg[0-9]+]]: {{.*}}, [[F:%arg[0-9]+]]: {{.*}}, [[G:%arg[0-9]+]]: {{.*}})
// CHECK: [[BC:%[0-9]+]] = "geomalg.cmul"([[B]], [[C]])
// CHECK: [[EF:%[0-9]+]] = "geomalg.cmul"([[E]], [[F]])
// CHECK: [[S:%[0-9]+]] = "geomalg.sum"([[A]], [[BC]])
// CHECK: [[T:%[0-9]+]] = "geomalg.sum"([[D]], [[EF]])
// CHECK: [[ST:%[0-9]+]] = "geomalg.cmul"([[S]], [[T]])
// CHECK: [[GG:%[0-9]+]] = "geomalg.cmul"([[G]], [[G]])
// CHECK: [[NGG:%[0-9]+]] = "geomalg.negate"([[GG]])
// CHECK: "geomalg.sum"([[ST]], [[GG]], [[NGG]])
func.func @no_growth(%a: !scalar, %b: !scalar, %c: !scalar, %d: !scalar,
                     %e: !scalar, %f: !scalar, %g: !scalar) -> !scalar {
  %bc = "geomalg.cmul"(%b, %c) : (!scalar, !scalar) -> !scalar
  %ef = "geomalg.cmul"(%e, %f) : (!scalar, !scalar) -> !scalar
  %s = "geomalg.sum"(%a, %bc) : (!scalar, !scalar) -> !scalar
  %t = "geomalg.sum"(%d, %ef) : (!scalar, !scalar) -> !scalar
  %0 = "geomalg.cmul"(%s, %t) : (!scalar, !scalar) -> !scalar
  %1 = "geomalg.cmul"(%g, %g) : (!scalar, !scalar) -> !scalar
  %2 = "geomalg.negate"(%1) : (!scalar) -> !scalar
  %3 = "geomalg.sum"(%0, %1, %2) : (!scalar, !scalar, !scalar) -> !scalar
  geomalg.return %3 : !scalar
}
