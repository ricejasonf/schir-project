// RUN: geomalg-opt --geomalg-expand="simplify=false enable-patterns={ExpandVersorVP, ExpandVersorUnary, ExpandVersorOperand, UpdateInferredTypes}" %s \
// RUN:| FileCheck %s

// RUN: geomalg-opt --geomalg-expand="metric=cga" --geomalg-lower %s \
// RUN:| FileCheck --check-prefix="LOWER" %s

!e1 = !geomalg.blade<1>
!e2 = !geomalg.blade<2>
!vec2 = !geomalg.multivector<<1>, <2>>
!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!v2 = !geomalg.versor<!vec3, !e1>

// Nested versors are flattened and applied last factor first.
// CHECK-LABEL: func.func @vprod_versor_op
// CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[A:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[B:%arg[0-9]+]]: !geomalg.blade<1>
// CHECK-SAME: [[C:%arg[0-9]+]]: !geomalg.blade<2>
// CHECK-NEXT: [[VP:%[0-9]+]] = "geomalg.vprod"([[X]], [[C]], [[B]], [[A]])
// CHECK-NEXT: geomalg.return [[VP]]
func.func @vprod_versor_op(%x: !vec3, %a: !vec3, %b: !e1, %c: !e2)
    -> !geomalg.unknown {
  %0 = "geomalg.versor"(%a, %b) : (!vec3, !e1) -> !v2
  %1 = "geomalg.versor"(%0, %c) : (!v2, !e2) -> !geomalg.unknown
  %2 = "geomalg.vprod"(%x, %1) : (!vec3, !geomalg.unknown) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

// CHECK-LABEL: func.func @vprod_versor_arg
// CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor<!geomalg.multivector<<1>, <2>, <4>>, !geomalg.blade<1>>
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-SAME: -> (!geomalg.multivector<<1>, <2>, <4>>, !geomalg.blade<1>)
// CHECK-NEXT: [[VP:%[0-9]+]] = "geomalg.vprod"([[X]], [[F]]#1, [[F]]#0)
// CHECK-NEXT: geomalg.return [[VP]]
func.func @vprod_versor_arg(%x: !vec3, %v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %v) : (!vec3, !v2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @inverse
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-NEXT: [[I1:%[0-9]+]] = "geomalg.inverse"([[F]]#1)
// CHECK-NEXT: [[I0:%[0-9]+]] = "geomalg.inverse"([[F]]#0)
// CHECK-NEXT: [[R:%[0-9]+]] = "geomalg.versor"([[I1]], [[I0]])
// CHECK-NEXT: geomalg.return [[R]]
func.func @inverse(%v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.inverse"(%v) : (!v2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @reverse
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor
// CHECK-SAME: -> !geomalg.versor<!geomalg.blade<1>, !geomalg.multivector<<1>, <2>, <4>>>
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-NEXT: [[R:%[0-9]+]] = "geomalg.versor"([[F]]#1, [[F]]#0)
// CHECK-NEXT: geomalg.return [[R]]
func.func @reverse(%v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.reverse"(%v) : (!v2) -> !geomalg.versor<!e1, !vec3>
  geomalg.return %0 : !geomalg.versor<!e1, !vec3>
}

// CHECK-LABEL: func.func @grade_invo
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-NEXT: [[G0:%[0-9]+]] = "geomalg.grade_invo"([[F]]#0)
// CHECK-NEXT: [[G1:%[0-9]+]] = "geomalg.grade_invo"([[F]]#1)
// CHECK-NEXT: [[R:%[0-9]+]] = "geomalg.versor"([[G0]], [[G1]])
// CHECK-NEXT: geomalg.return [[R]]
func.func @grade_invo(%v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.grade_invo"(%v) : (!v2) -> !v2
  geomalg.return %0 : !v2
}

// CHECK-LABEL: func.func @negate
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-NEXT: [[N:%[0-9]+]] = "geomalg.negate"([[F]]#0)
// CHECK-NEXT: [[R:%[0-9]+]] = "geomalg.versor"([[N]], [[F]]#1)
// CHECK-NEXT: geomalg.return [[R]]
func.func @negate(%v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.negate"(%v) : (!v2) -> !v2
  geomalg.return %0 : !v2
}

// Other operations use the geometric product of the factors.
// CHECK-LABEL: func.func @gprod
// CHECK-SAME: [[X:%arg[0-9]+]]: !geomalg.multivector<<1>, <2>, <4>>
// CHECK-SAME: [[V:%arg[0-9]+]]: !geomalg.versor
// CHECK-NEXT: [[F:%[0-9]+]]:2 = "geomalg.factors"([[V]])
// CHECK-NEXT: [[P:%[0-9]+]] = "geomalg.gprod"([[F]]#0, [[F]]#1)
// CHECK-NEXT: [[R:%[0-9]+]] = "geomalg.gprod"([[P]], [[X]])
// CHECK-NEXT: geomalg.return [[R]]
func.func @gprod(%x: !vec3, %v: !v2) -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%v, %x) : (!v2, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// Versors are stored as the concatenated elements of their factors.
// LOWER-LABEL: func.func @make_versor
// LOWER-SAME: ([[A:%arg[0-9]+]]: vector<2xf32>, [[B:%arg[0-9]+]]: f32)
// LOWER-SAME: -> vector<3xf32>
// LOWER-NEXT: [[E:%[0-9]+]]:2 = vector.to_elements [[A]]
// LOWER-NEXT: [[R:%[0-9]+]] = vector.from_elements [[E]]#0, [[E]]#1, [[B]] : vector<3xf32>
// LOWER-NEXT: return [[R]]
func.func @make_versor(%a: !vec2, %b: !e1) -> !geomalg.unknown {
  %0 = "geomalg.versor"(%a, %b) : (!vec2, !e1) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// LOWER-LABEL: func.func @versor_factors
// LOWER-SAME: [[V:%arg[0-9]+]]: vector<3xf32>
// LOWER-SAME: -> vector<2xf32>
// LOWER-NEXT: [[E:%[0-9]+]]:3 = vector.to_elements [[V]]
// LOWER-NEXT: [[A:%[0-9]+]] = vector.from_elements [[E]]#0, [[E]]#1 : vector<2xf32>
// LOWER-NEXT: return [[A]]
func.func @versor_factors(%v: !geomalg.versor<!vec2, !e1>) -> !vec2 {
  %0:2 = "geomalg.factors"(%v) : (!geomalg.versor<!vec2, !e1>) -> (!vec2, !e1)
  geomalg.return %0#0 : !vec2
}
