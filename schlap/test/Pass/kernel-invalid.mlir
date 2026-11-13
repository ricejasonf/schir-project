// RUN: schlap-opt %s -split-input-file -verify-diagnostics -schlap-check-proofs

schlap.theorem @self {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{'self' refers to itself}}
  %0 = schlap.ref @self : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.theorem @early {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{'late' is not declared before this}}
  %0 = schlap.ref @late : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

schlap.theorem @late {
  %0 = schlap.true : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  %0 = schlap.top_intro : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.theorem @failed {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{expected '⊥' but got '⊤'}}
  %0 = schlap.top_intro : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

schlap.theorem @uses_failed {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{'failed' did not check}}
  %0 = schlap.ref @failed : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.theorem @not_kernel {
  %0 = schlap.int 1 : !schlap.unknown
  // expected-error@+1 {{'schlap.add' is not a term of the proof kernel}}
  %1 = schlap.add %0, %0 : !schlap.unknown
  %2 = schlap.eq %1, %1 : !schlap.unknown
  schlap.yield %2 : !schlap.unknown
} proof {
  %0 = schlap.refl : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.theorem @negative {
  // expected-error@+1 {{the integer -1 is not a natural number from 0 to 4096}}
  %0 = schlap.int -1 : !schlap.unknown
  %1 = schlap.eq %0, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} proof {
  %0 = schlap.refl : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.theorem @too_large {
  // expected-error@+1 {{the integer 4097 is not a natural number from 0 to 4096}}
  %0 = schlap.int 4097 : !schlap.unknown
  %1 = schlap.eq %0, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} proof {
  %0 = schlap.refl : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

schlap.axiom @type_of_type {
  // expected-error@+1 {{Type has no type}}
  %0 = schlap.type_sort : !schlap.unknown
  %1 = schlap.eq %0, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
}

// -----

schlap.theorem @no_yield {
  // expected-error@+1 {{block with no terminator}}
  %0 = schlap.true : !schlap.unknown
} proof {
  %0 = schlap.top_intro : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

// expected-error@+1 {{'schlap.lambda' op expects 1 bound variables per region}}
%0 = schlap.lambda {
  %1 = schlap.true : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} : !schlap.unknown
