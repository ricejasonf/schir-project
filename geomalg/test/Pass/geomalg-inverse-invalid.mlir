// RUN: geomalg-opt --geomalg-expand="metric=cga" %s -split-input-file -verify-diagnostics

// An odd multivector is not assumed to be a versor.
func.func @inverse_odd(%v: !geomalg.multivector<<1>, <7>>) -> !geomalg.unknown {
  // expected-error@+1 {{expecting a blade, 1-vector, versor, or even multivector operand}}
  %0 = "geomalg.inverse"(%v) : (!geomalg.multivector<<1>, <7>>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// -----

// A trivector that is not a single blade.
func.func @inverse_trivector(%v: !geomalg.multivector<<7>, <11>>) -> !geomalg.unknown {
  // expected-error@+1 {{expecting a blade, 1-vector, versor, or even multivector operand}}
  %0 = "geomalg.inverse"(%v) : (!geomalg.multivector<<7>, <11>>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// -----

// The operand type is only known after expanding the geometric product
// of a 1-vector and an even versor.
func.func @inverse_inferred_odd(%a: !geomalg.blade<1>, %m: !geomalg.multivector<<0>, <6>>) -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%a, %m) : (!geomalg.blade<1>, !geomalg.multivector<<0>, <6>>) -> !geomalg.unknown
  // expected-error@+1 {{expecting a blade, 1-vector, versor, or even multivector operand}}
  %1 = "geomalg.inverse"(%0) : (!geomalg.unknown) -> !geomalg.unknown
  geomalg.return %1 : !geomalg.unknown
}
