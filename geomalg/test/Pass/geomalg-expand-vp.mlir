// RUN: geomalg-opt \
// RUN:--geomalg-expand="simplify=false enable-patterns={ExpandVP, DistributeVP}" %s \
// RUN:| FileCheck %s

// RUN: geomalg-opt --geomalg-expand="metric=cga disable-patterns={ExpandMatvec}" \
// RUN:             --geomalg-simplify %s \
// RUN:| FileCheck --check-prefix="CGA"  %s

// CHECK-LABEL: func.func @versor_prod_0
// CHECK-SAME: ([[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>)
// CHECK: return [[ARG0]]
func.func @versor_prod_0(%arg0: !geomalg.multivector<<1>, <2>, <4>>)
            -> !geomalg.multivector<<1>, <2>, <4>> {
  %0 = "geomalg.vprod"(%arg0)
    : (!geomalg.multivector<<1>, <2>, <4>>)
      -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @versor_prod_1
// CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[ARG1:%arg[0-9]+]]: !geomalg.blade<1>
// The reflection of a vector is v a v⁻¹ = 2 (v ⌋ a) v⁻¹ - a.
// CHECK: [[MV:%[0-9]+]]:3 = "geomalg.expand"(%arg0)
// CHECK: [[TWO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 2.0{{0*}}e+00 : f32}>
// CHECK: [[D0:%[0-9]+]] = "geomalg.iprod"([[ARG1]], [[MV]]#0)
// CHECK: [[S0:%[0-9]+]] = "geomalg.iprod"([[TWO]], [[D0]])
// CHECK: [[N0:%[0-9]+]] = "geomalg.negate"([[MV]]#0)
// CHECK: [[I0:%[0-9]+]] = "geomalg.inverse"([[ARG1]])
// CHECK: [[R0:%[0-9]+]] = "geomalg.iprod"([[S0]], [[I0]])
// CHECK: "geomalg.sum"([[R0]], [[N0]],
// CHECK-NOT: "geomalg.gprod"
// CHECK: return
func.func @versor_prod_1(%arg0: !geomalg.multivector<<1>, <2>, <4>>,
                         %arg1: !geomalg.blade<1>)
            -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1)
    : (!geomalg.multivector<<1>, <2>, <4>>, !geomalg.blade<1>)
      -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @versor_prod_2
// CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[ARG1:%arg[0-9]+]]: !geomalg.blade<1>
// CHECK-SAME: [[ARG2:%arg[0-9]+]]: !geomalg.blade<2>
// The first versor is applied first.
// CHECK-NOT: "geomalg.vprod"
// CHECK: "geomalg.inverse"([[ARG1]])
// CHECK: "geomalg.inverse"([[ARG2]])
// CHECK-NOT: "geomalg.vprod"
// CHECK: return
func.func @versor_prod_2(%arg0: !geomalg.multivector<<1>, <2>, <4>>,
                         %arg1: !geomalg.blade<1>, %arg2: !geomalg.blade<2>)
            -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1, %arg2)
    : (!geomalg.multivector<<1>, <2>, <4>>,
        !geomalg.blade<1>, !geomalg.blade<2>)
      -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// This also tests the geometric product on basis blades
// with grade k > 1.
// CGA-LABEL: func.func @versor_prod_3
// CGA-CHECK: return %{{[0-9]+}} : !geomalg.blade<4>
func.func @versor_prod_3(%arg0: !geomalg.blade<2147483651>,
                         %arg1: !geomalg.blade<4>)
                          -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg1, %arg0)
    : (!geomalg.blade<4>, !geomalg.blade<2147483651>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// Points are closed under the versor product.
!vec2 = !geomalg.multivector<<1>, <2>>
!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!uvec3 = !geomalg.unit_vector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
!scalar = !geomalg.blade<0>
!e1 = !geomalg.blade<1>
!e2 = !geomalg.blade<2>
!e3 = !geomalg.blade<4>
!no = !geomalg.blade<8>
!ni = !geomalg.blade<16>

// CGA-LABEL: func.func @point_refl_1
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_1(%arg0: !point, %arg1: !vec2) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1)
    : (!point, !vec2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_2
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_2(%arg0: !point, %arg1: !vec3) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1)
    : (!point, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_3
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_3(%arg0: !point, %arg1: !vec2, %arg2: !vec3)
    -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1, %arg2)
    : (!point, !vec2, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_4
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_4(%arg0: !point, %arg1: !vec3, %arg2: !vec2)
    -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1, %arg2)
    : (!point, !vec3, !vec2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_5
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_5(%arg0: !point, %arg1: !vec3, %arg2: !vec2, %arg3: !vec3)
    -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1, %arg2, %arg3)
    : (!point, !vec3, !vec2, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_e1
// CGA-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CGA-NOT: "geomalg.inverse"
// CGA: "geomalg.dot"([[ARG0]], [[ARG0]])
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @point_refl_e1(%arg0: !vec3)
    -> !geomalg.unknown {
  %1 = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !no
  %2 = "geomalg.blade"() <{coefficient = 5.000000e-01 : f32}> : () -> !scalar
  %3 = "geomalg.dot"(%arg0, %arg0) : (!vec3, !vec3) -> !scalar
  %4 = "geomalg.cmul"(%2, %3) : (!scalar, !scalar) -> !ni
  %5 = "geomalg.sum"(%1, %arg0, %4) : (!no, !vec3, !ni) -> !geomalg.unknown
  %6 = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !e1
  %0 = "geomalg.vprod"(%5, %6)
    : (!geomalg.unknown, !e1) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CGA-LABEL: func.func @point_refl_e1_simplified
// CGA-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CGA-NOT: "geomalg.inverse"
// CGA-NOT: "geomalg.dot"([[ARG0]], [[ARG0]])
// CGA-NOT: "geomalg.convert"
// CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>>
func.func @point_refl_e1_simplified(%arg0: !vec3)
    -> !geomalg.multivector<<1>, <2>, <4>> {
  %1 = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !no
  %2 = "geomalg.blade"() <{coefficient = 5.000000e-01 : f32}> : () -> !scalar
  %3 = "geomalg.dot"(%arg0, %arg0) : (!vec3, !vec3) -> !scalar
  %4 = "geomalg.cmul"(%2, %3) : (!scalar, !scalar) -> !ni
  %5 = "geomalg.sum"(%1, %arg0, %4) : (!no, !vec3, !ni) -> !geomalg.unknown
  %6 = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !e1
  %7 = "geomalg.vprod"(%5, %6)
    : (!geomalg.unknown, !e1) -> !geomalg.unknown
  %8 = "geomalg.convert"(%7) : (!geomalg.unknown)
    -> !geomalg.multivector<!e1, !e2, !e3, !no, !ni>
  %9:5 = "geomalg.expand"(%8) : (!geomalg.multivector<!e1, !e2, !e3, !no, !ni>)
    -> (!e1, !e2, !e3, !no, !ni)
  %10 = "geomalg.sum"(%9#0, %9#1, %9#2)
    : (!e1, !e2, !e3) -> !geomalg.multivector<<1>, <2>, <4>>
  geomalg.return %10 : !geomalg.multivector<<1>, <2>, <4>>
}
