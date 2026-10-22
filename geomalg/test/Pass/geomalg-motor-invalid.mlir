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

// -----

func.func @normalize_odd(%v: !geomalg.multivector<<1>, <2>>) {
  // expected-error@+1 {{expecting an even operand}}
  %0 = "geomalg.normalize"(%v) : (!geomalg.multivector<<1>, <2>>) -> !geomalg.unknown
  return
}

// -----

func.func @interp_method(%m: !geomalg.multivector<<0>, <3>>, %t: !geomalg.blade<0>) {
  // expected-error@+2 {{expected ::geomalg::InterpMethod to be one of: exact, nlerp}}
  // expected-error@+1 {{failed to parse Geomalg_InterpMethodAttr parameter 'value'}}
  %0 = "geomalg.interp"(%m, %m, %t) <{method = #geomalg.interp_method<slerpz>}> : (!geomalg.multivector<<0>, <3>>, !geomalg.multivector<<0>, <3>>, !geomalg.blade<0>) -> !geomalg.unknown
  return
}

// -----

func.func @interp_odd_versor(%m: !geomalg.multivector<<0>, <3>>, %v: !geomalg.versor<!geomalg.blade<1>>, %t: !geomalg.blade<0>) {
  // expected-error@+1 {{expecting an even versor operand}}
  %0 = "geomalg.interp"(%m, %v, %t) : (!geomalg.multivector<<0>, <3>>, !geomalg.versor<!geomalg.blade<1>>, !geomalg.blade<0>) -> !geomalg.unknown
  return
}

// -----

// expected-error@+1 {{expecting a blade of grade other than 1 (use unit_vector)}}
func.func @unit_multivector_vector(%v: !geomalg.unit_multivector<<1>, <2>>) {
  return
}
