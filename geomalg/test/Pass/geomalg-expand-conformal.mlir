// RUN: geomalg-opt --geomalg-expand="metric=cga" %s | FileCheck %s

// RUN: geomalg-opt --geomalg-expand="metric=cga" --geomalg-lower %s \
// RUN:| FileCheck --check-prefix="LOWER" %s

!scalar = !geomalg.blade<0>
!e1 = !geomalg.blade<1>
!e3 = !geomalg.blade<4>
!e23 = !geomalg.blade<6>
!no = !geomalg.blade<8>
!ni = !geomalg.blade<16>
!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!uvec3 = !geomalg.unit_vector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
!plane = !geomalg.multivector<<1>, <2>, <4>, <16>>
!e3_ni = !geomalg.multivector<<4>, <16>>

// Scalars and orthogonal blades of even grade commute with a 1-vector.
// CHECK-LABEL: func.func @orthogonal_scalar
// CHECK-SAME: ([[X:%arg[0-9]+]]: !geomalg.blade<0>
// CHECK-NEXT: geomalg.return [[X]] : !geomalg.blade<0>
func.func @orthogonal_scalar(%x: !scalar, %v: !e1) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %v) : (!scalar, !e1) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @orthogonal_bivector
// CHECK-SAME: ([[X:%arg[0-9]+]]: !geomalg.blade<6>
// CHECK-NEXT: geomalg.return [[X]] : !geomalg.blade<6>
func.func @orthogonal_bivector(%x: !e23, %v: !e1) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %v) : (!e23, !e1) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @orthogonal_vector
// CHECK-SAME: ([[X:%arg[0-9]+]]: !geomalg.blade<4>
// CHECK: [[N:%[0-9]+]] = "geomalg.negate"
// CHECK-NEXT: geomalg.return [[N]] : !geomalg.blade<4>
func.func @orthogonal_vector(%x: !e3, %v: !e1) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %v) : (!e3, !e1) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A reflection of a k-blade is the outer product of its reflected factors.
// CHECK-LABEL: func.func @reflect_bivector
// CHECK-SAME: -> !geomalg.multivector<<3>, <5>, <6>> {
// CHECK-NOT: "geomalg.gprod"
// CHECK: geomalg.return
func.func @reflect_bivector(%x: !geomalg.blade<3>, %v: !vec3)
    -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %v) : (!geomalg.blade<3>, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The dot product requires an orthonormal basis so ni² = 0 is not used.
// CHECK-LABEL: func.func @square_euclidean
// CHECK: "geomalg.dot"
func.func @square_euclidean(%v: !vec3) -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%v, %v) : (!vec3, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @square_null
// CHECK-NEXT: [[V:%[0-9]+]]:2 = "geomalg.expand"
// CHECK-NEXT: [[SQ:%[0-9]+]] = "geomalg.cmul"([[V]]#0, [[V]]#0)
// CHECK-NEXT: geomalg.return [[SQ]] : !geomalg.blade<0>
func.func @square_null(%v: !e3_ni) -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%v, %v) : (!e3_ni, !e3_ni) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A conformal point X = o + x + ½x²∞.
func.func private @up(%x: !vec3) -> !point {
  %o = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !no
  %i = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !ni
  %h = "geomalg.blade"() <{coefficient = 0.5 : f32}> : () -> !scalar
  %0 = "geomalg.iprod"(%x, %x) : (!vec3, !vec3) -> !geomalg.unknown
  %1 = "geomalg.iprod"(%h, %0) : (!scalar, !geomalg.unknown) -> !geomalg.unknown
  %2 = "geomalg.iprod"(%1, %i) : (!geomalg.unknown, !ni) -> !geomalg.unknown
  %3 = "geomalg.sum"(%o, %x, %2) : (!no, !vec3, !geomalg.unknown) -> !geomalg.unknown
  %4 = "geomalg.convert"(%3) : (!geomalg.unknown) -> !point
  geomalg.return %4 : !point
}

// The reflection of a point in a dual plane π = n + δ∞ is a point.
// CHECK-LABEL: func.func @reflect_plane
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK-NOT: "geomalg.vprod"
// CHECK-NOT: "geomalg.gprod"
// CHECK: geomalg.return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// LOWER-LABEL: func.func @reflect_plane
// LOWER-SAME: (%{{.*}}: vector<5xf32>, %{{.*}}: vector<3xf32>, %{{.*}}: f32)
// LOWER-SAME: -> vector<5xf32>
func.func @reflect_plane(%X: !point, %n: !vec3, %d: !scalar)
    -> !geomalg.unknown {
  %i = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !ni
  %0 = "geomalg.iprod"(%d, %i) : (!scalar, !ni) -> !geomalg.unknown
  %1 = "geomalg.sum"(%n, %0) : (!vec3, !geomalg.unknown) -> !geomalg.unknown
  %2 = "geomalg.vprod"(%X, %1) : (!point, !geomalg.unknown) -> !geomalg.unknown
  geomalg.return %2 : !geomalg.unknown
}

// A translator T = (t + ½t²∞) t is a versor of two parallel dual planes.
// CHECK-LABEL: func.func @translate
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK-NOT: "geomalg.versor"
// CHECK-NOT: "geomalg.vprod"
// CHECK-NOT: "geomalg.gprod"
// CHECK: geomalg.return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// LOWER-LABEL: func.func @translate
// LOWER-SAME: -> vector<5xf32>
func.func @translate(%x: !vec3, %t: !vec3) -> !geomalg.unknown {
  %X = "geomalg.call"(%x) <{callee = @up}> : (!vec3) -> !point
  %i = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !ni
  %h = "geomalg.blade"() <{coefficient = 0.5 : f32}> : () -> !scalar
  %0 = "geomalg.iprod"(%t, %t) : (!vec3, !vec3) -> !geomalg.unknown
  %1 = "geomalg.iprod"(%h, %0) : (!scalar, !geomalg.unknown) -> !geomalg.unknown
  %2 = "geomalg.iprod"(%1, %i) : (!geomalg.unknown, !ni) -> !geomalg.unknown
  %3 = "geomalg.sum"(%t, %2) : (!vec3, !geomalg.unknown) -> !geomalg.unknown
  %T = "geomalg.versor"(%3, %t) : (!geomalg.unknown, !vec3) -> !geomalg.unknown
  %4 = "geomalg.vprod"(%X, %T) : (!point, !geomalg.unknown) -> !geomalg.unknown
  geomalg.return %4 : !geomalg.unknown
}

// A dilator is a versor of two concentric dual spheres σ = X - ½ρ²∞.
// CHECK-LABEL: func.func @dilate
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK-NOT: "geomalg.versor"
// CHECK-NOT: "geomalg.vprod"
// CHECK: geomalg.return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
func.func @dilate(%X: !point, %C: !point, %s: !scalar) -> !geomalg.unknown {
  %i = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !ni
  %h = "geomalg.blade"() <{coefficient = -0.5 : f32}> : () -> !scalar
  %0 = "geomalg.iprod"(%h, %s) : (!scalar, !scalar) -> !geomalg.unknown
  %1 = "geomalg.iprod"(%0, %i) : (!geomalg.unknown, !ni) -> !geomalg.unknown
  %2 = "geomalg.sum"(%C, %1) : (!point, !geomalg.unknown) -> !geomalg.unknown
  %3 = "geomalg.iprod"(%h, %i) : (!scalar, !ni) -> !geomalg.unknown
  %4 = "geomalg.sum"(%C, %3) : (!point, !geomalg.unknown) -> !geomalg.unknown
  %D = "geomalg.versor"(%2, %4) : (!geomalg.unknown, !geomalg.unknown)
    -> !geomalg.unknown
  %5 = "geomalg.vprod"(%X, %D) : (!point, !geomalg.unknown) -> !geomalg.unknown
  geomalg.return %5 : !geomalg.unknown
}

// CHECK-LABEL: func.func @dot_unit_vector
// CHECK-NEXT: [[X:%[0-9]+]] = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !geomalg.blade<0>
// CHECK-NEXT: geomalg.return [[X]] : !geomalg.blade<0>
func.func @dot_unit_vector(%u: !uvec3) -> !geomalg.unknown {
  %0 = "geomalg.dot"(%u, %u) : (!uvec3, !uvec3) -> !scalar
  geomalg.return %0 : !scalar
}

// CHECK-LABEL: func.func @iprod_unit_vector
// CHECK-NEXT: [[X:%[0-9]+]] = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !geomalg.blade<0>
// CHECK-NEXT: geomalg.return [[X]] : !geomalg.blade<0>
func.func @iprod_unit_vector(%u: !uvec3) -> !geomalg.unknown {
  %0 = "geomalg.iprod"(%u, %u) : (!uvec3, !uvec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}
