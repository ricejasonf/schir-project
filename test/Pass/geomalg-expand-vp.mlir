// RUN: geomalg-opt \
// RUN:--geomalg-expand="enable-patterns={ExpandVP, DistributeVP}" --cse %s \
// RUN:| FileCheck %s

// RUN: geomalg-opt --geomalg-expand="metric=cga" --geomalg-simplify %s \
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
// CHECK: [[MV:%[0-9]+]]:3 = "geomalg.expand"(%arg0)
// CHECK: [[I0:%[0-9]+]] = "geomalg.inverse"([[ARG1]])
// CHECK: [[P0_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#0, [[I0]])
// CHECK: [[P0_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P0_0]])
// CHECK: [[P1_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#1, [[I0]])
// CHECK: [[P1_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P1_0]])
// CHECK: [[P2_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#2, [[I0]])
// CHECK: [[P2_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P2_0]])
// CHECK: [[SUM:%[0-9]+]] = "geomalg.sum"([[P0_1]], [[P1_1]], [[P2_1]])
// CHECK: return [[SUM]]
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
// CHECK: [[MV:%[0-9]+]]:3 = "geomalg.expand"(%arg0)
// CHECK: [[I0:%[0-9]+]] = "geomalg.inverse"([[ARG1]])
// CHECK: [[P0_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#0, [[I0]])
// CHECK: [[P0_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P0_0]])
// CHECK: [[VP0:%[0-9]+]] = "geomalg.vprod"([[P0_1]], [[ARG2]])
// CHECK: [[P1_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#1, [[I0]])
// CHECK: [[P1_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P1_0]])
// CHECK: [[VP1:%[0-9]+]] = "geomalg.vprod"([[P1_1]], [[ARG2]])
// CHECK: [[P2_0:%[0-9]+]] = "geomalg.gprod"([[MV]]#2, [[I0]])
// CHECK: [[P2_1:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[P2_0]])
// CHECK: [[VP2:%[0-9]+]] = "geomalg.vprod"([[P2_1]], [[ARG2]])
// CHECK: [[SUM:%[0-9]+]] = "geomalg.sum"([[VP0]], [[VP1]], [[VP2]])
// CHECK: return [[SUM]]
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
  %8:3 = "geomalg.expand"(%7) : (!geomalg.unknown) -> (!e1, !e2, !e3)
  %9 = "geomalg.sum"(%8#0, %8#1, %8#2)
    : (!e1, !e2, !e3) -> !geomalg.multivector<<1>, <2>, <4>>
  geomalg.return %9 : !geomalg.multivector<<1>, <2>, <4>>
}
