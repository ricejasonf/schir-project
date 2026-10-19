// RUN: geomalg-opt --split-input-file --verify-diagnostics %s

// expected-error@+1 {{expecting 1-vector versor factor: '!geomalg.blade<3>'}}
func.func @bivector_factor(%arg0: !geomalg.versor<!geomalg.blade<3>>)

// -----

// expected-error@+1 {{expecting 1-vector versor factor: '!geomalg.versor<!geomalg.blade<1>>'}}
func.func @versor_factor(%arg0: !geomalg.versor<!geomalg.versor<!geomalg.blade<1>>>)

// -----

func.func @scalar_operand(%arg0: !geomalg.blade<0>) -> !geomalg.unknown {
  // expected-error@+1 {{operand #0 must be variadic of 1-vector or versor}}
  %0 = "geomalg.versor"(%arg0) : (!geomalg.blade<0>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// -----

func.func @no_operands() -> !geomalg.unknown {
  // expected-error@+1 {{expecting at least one factor}}
  %0 = "geomalg.versor"() : () -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}
