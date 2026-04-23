// RUN: geomalg-opt \
// RUN: --geomalg-to-spirv \
// RUN: %s | FileCheck %s

!s = !geomalg.blade<0>
!e1 = !geomalg.blade<1>
!e2 = !geomalg.blade<2>
!e3 = !geomalg.blade<4>
!no = !geomalg.blade<8>
!ni = !geomalg.blade<16>
!vec1 = !geomalg.multivector<<1>>
!vec2 = !geomalg.multivector<<1>, <2>>
!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!uvec2 = !geomalg.unit_vector<<1>, <2>>
!uvec3 = !geomalg.unit_vector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
module {
// CHECK-LABEL: spirv.func @gprod
// CHECK-SAME: %[[ARG0:arg[0-9]*]]: vector<3xf32>
// CHECK-SAME: %[[ARG1:arg[0-9]*]]: vector<3xf32>
// CHECK-COUNT-9: spirv.FMul
// CHECK-NOT: spirv.FMul
// CHECK-COUNT-5: spirv.FAdd
// CHECK-NOT: spirv.FAdd
// CHECK: spirv.Return
func.func @gprod(%arg0: !vec3, %arg1: !vec3)
        -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%arg0, %arg1)
    : (!vec3, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: spirv.func @reflect_point
// CHECK-SAME: %[[ARG0:arg[0-9]*]]: vector<3xf32>
// CHECK-SAME: %[[ARG1:arg[0-9]*]]: vector<3xf32>
// CHECK: spirv.Return
func.func @reflect_point(%arg0: !vec3, %arg1: !uvec3)
        -> !geomalg.unknown {
  %1 = "geomalg.blade"() <{coefficient = 1.000000e+00 : f32}> : () -> !no
  %2 = "geomalg.blade"() <{coefficient = 5.000000e-01 : f32}> : () -> !s
  %3 = "geomalg.dot"(%arg0, %arg0) : (!vec3, !vec3) -> !s
  %4 = "geomalg.cmul"(%2, %3) : (!s, !s) -> !ni
  %5 = "geomalg.sum"(%1, %arg0, %4) : (!no, !vec3, !ni) -> !geomalg.unknown
  %0 = "geomalg.vprod"(%5, %arg1)
    : (!geomalg.unknown, !uvec3) -> !geomalg.unknown
  %8 = "geomalg.convert"(%0) : (!geomalg.unknown)
    -> !geomalg.multivector<!e1, !e2, !e3, !no, !ni>
  %9:5 = "geomalg.expand"(%8) : (!geomalg.multivector<!e1, !e2, !e3, !no, !ni>)
    -> (!e1, !e2, !e3, !no, !ni)
  %10 = "geomalg.sum"(%9#0, %9#1, %9#2)
    : (!e1, !e2, !e3) -> !vec3
  geomalg.return %10 : !vec3
}

// CHECK-LABEL: spirv.func @rotate_point
// CHECK-SAME: %[[ARG0:arg[0-9]*]]: vector<3xf32>
// CHECK: spirv.Return
func.func @rotate_point(%arg0: !vec3) -> !geomalg.unknown {
  %no = "geomalg.blade"() <{coefficient = 1.0e+00 : f32}> : () -> !no
  %1 = "geomalg.blade"() <{coefficient = 1.0e+00 : f32}> : () -> !e1
  %2 = "geomalg.blade"() <{coefficient = 1.0e+00 : f32}> : () -> !e2
  %3 = "geomalg.sum"(%1, %2) : (!e1, !e2) -> !vec2
  %u3 = "geomalg.convert"(%3) : (!vec2) -> !uvec2
  %4 = "geomalg.gprod"(%1, %u3) : (!e1, !uvec2) -> !geomalg.unknown
  %neg4 = "geomalg.negate"(%4) : (!geomalg.unknown) -> !geomalg.unknown
  %5 = "geomalg.gprod"(%3, %1) : (!vec2, !e1) -> !geomalg.unknown
  %6 = "geomalg.gprod"(%neg4, %arg0)
    : (!geomalg.unknown, !vec3) -> !geomalg.unknown
  %0 = "geomalg.gprod"(%6, %5)
    : (!geomalg.unknown, !geomalg.unknown) -> !geomalg.unknown
  %sum = "geomalg.sum"(%0, %no) : (!geomalg.unknown, !no) -> !geomalg.unknown
  geomalg.return %sum : !geomalg.unknown
}

}

