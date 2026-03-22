// RUN: geomalg-opt \
// RUN:--geomalg-expand="enable-patterns={ExpandVP, DistributeVP}" %s \
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
      -> !geomalg.multivector<<1>, <2>, <4>>
  geomalg.return %0 : !geomalg.multivector<<1>, <2>, <4>>
}

// CHECK-LABEL: func.func @versor_prod_1
// CHECK-SAME: [[ARG0:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[ARG1:%arg[0-9]+]]: !geomalg.blade<1>
// CHECK: [[P0:%[0-9]+]] = "geomalg.gprod"([[ARG1]], [[ARG0]])
// CHECK: [[I0:%[0-9]+]] = "geomalg.inverse"([[ARG1]])
// CHECK: [[P1:%[0-9]+]] = "geomalg.gprod"([[P0]], [[I0]])
// CHECK: return [[P1]]
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
// CHECK: [[P0:%[0-9]+]] = "geomalg.gprod"([[ARG2]], [[ARG1]])
// CHECK: [[P1:%[0-9]+]] = "geomalg.gprod"([[P0]], [[ARG0]])
// CHECK: [[I0:%[0-9]+]] = "geomalg.inverse"([[ARG1]])
// CHECK: [[P2:%[0-9]+]] = "geomalg.gprod"([[P1]], [[I0]])
// CHECK: [[I1:%[0-9]+]] = "geomalg.inverse"([[ARG2]])
// CHECK: [[P3:%[0-9]+]] = "geomalg.gprod"([[P2]], [[I1]])
// CHECK: return [[P3]]
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
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>

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

// COM-CGA-LABEL: func.func @point_refl_5
// COM-CGA: return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// FIXME runaway pass
//func.func @point_refl_5(%arg0: !point, %arg1: !vec3, %arg2: !vec2, %arg3: !vec3)
//    -> !geomalg.unknown {
//  %0 = "geomalg.vprod"(%arg0, %arg1, %arg2, %arg3)
//    : (!point, !vec3, !vec2, !vec3) -> !geomalg.unknown
//  geomalg.return %0 : !geomalg.unknown
//}
