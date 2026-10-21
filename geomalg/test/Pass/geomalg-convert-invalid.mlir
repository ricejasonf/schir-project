// RUN: geomalg-opt --geomalg-expand="metric=cga" %s -split-input-file -verify-diagnostics

!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>

// Converting a point to a 1-vector would drop its o and ∞ blades.
func.func @convert_drop_blades(%X: !point) -> !vec3 {
  // expected-error@+1 {{cannot convert '!geomalg.multivector<<1>, <2>, <4>, <8>, <16>>' to '!geomalg.multivector<<1>, <2>, <4>>'}}
  %0 = "geomalg.convert"(%X) : (!point) -> !vec3
  geomalg.return %0 : !vec3
}

// -----

!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
!plane = !geomalg.multivector<<1>, <2>, <4>, <16>>

// The argument type is only known after expanding the reflection.
func.func @convert_inferred(%X: !point, %p: !plane) -> !vec3 {
  %0 = "geomalg.vprod"(%X, %p) : (!point, !plane) -> !geomalg.unknown
  // expected-error@+1 {{cannot convert '!geomalg.multivector<<1>, <2>, <4>, <8>, <16>>' to '!geomalg.multivector<<1>, <2>, <4>>'}}
  %1 = "geomalg.convert"(%0) : (!geomalg.unknown) -> !vec3
  geomalg.return %1 : !vec3
}

// -----

!vec2 = !geomalg.multivector<<1>, <2>>
!uvec3 = !geomalg.unit_vector<<1>, <2>, <4>>

// A bivector is not a 1-vector to normalize.
func.func @convert_bivector_to_uv(%B: !geomalg.multivector<<3>, <5>>) -> !uvec3 {
  // expected-error@+1 {{cannot convert}}
  %0 = "geomalg.convert"(%B) : (!geomalg.multivector<<3>, <5>>) -> !uvec3
  geomalg.return %0 : !uvec3
}
