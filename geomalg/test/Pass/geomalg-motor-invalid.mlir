// RUN: geomalg-opt %s -split-input-file -verify-diagnostics

func.func @exp_vector(%v: !geomalg.multivector<<1>, <2>>) {
  // expected-error@+1 {{expecting a bivector operand}}
  %0 = "geomalg.exp"(%v) : (!geomalg.multivector<<1>, <2>>) -> !geomalg.unknown
  return
}

// -----

func.func @log_odd(%v: !geomalg.multivector<<0>, <7>>) {
  // expected-error@+1 {{expecting an even operand}}
  %0 = "geomalg.log"(%v) : (!geomalg.multivector<<0>, <7>>) -> !geomalg.unknown
  return
}

// -----

func.func @log_origin(%v: !geomalg.multivector<<0>, <9>>) {
  // expected-error@+1 {{expecting an operand without the origin}}
  %0 = "geomalg.log"(%v) : (!geomalg.multivector<<0>, <9>>) -> !geomalg.unknown
  return
}

// -----

func.func @log_odd_versor(%v: !geomalg.versor<!geomalg.blade<1>>) {
  // expected-error@+1 {{expecting an even versor operand}}
  %0 = "geomalg.log"(%v) : (!geomalg.versor<!geomalg.blade<1>>) -> !geomalg.unknown
  return
}
