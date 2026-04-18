// RUN: geomalg-opt \
// RUN: --geomalg-expand="metric=cga disable-patterns={ExpandMatvec}" \
// RUN: --geomalg-simplify \
// RUN: --geomalg-lower \
// RUN: --cse \
// RUN: --canonicalize \
// COM: --convert-tensor-to-spirv \
// RUN: --one-shot-bufferize="bufferize-function-boundaries function-boundary-type-conversion=identity-layout-map" \
// RUN: --buffer-results-to-out-params="modify-public-functions=true hoist-static-allocs=true" \
// RUN: --convert-arith-to-spirv \
// RUN: --convert-memref-to-spirv \
// RUN: --convert-func-to-spirv \
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

// Converting memref to spirv is not working out for the function arguments.
// (We don't really need function arguments for shaders I don't think.)
// CHECK: spirv.Return
module attributes {
  spirv.target_env = #spirv.target_env<#spirv.vce<v1.0, [], []>, #spirv.resource_limits<>>
} {
func.func @rotate_point(%arg0: !vec3, %arg1: !uvec3, %arg2: !uvec3)
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
}

