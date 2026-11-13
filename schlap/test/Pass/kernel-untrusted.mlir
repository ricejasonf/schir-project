// RUN: schlap-opt %s -split-input-file -verify-diagnostics -schlap-check-proofs | FileCheck %s

// Check a theorem that is marked checked in the input.
schlap.theorem @bogus attributes {axioms = [], checked} {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{expected '⊥' but got '⊤'}}
  %0 = schlap.top_intro : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

schlap.theorem @uses_bogus {
  %0 = schlap.false : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
} proof {
  // expected-error@+1 {{'bogus' did not check}}
  %0 = schlap.ref @bogus : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

// Check an axiom that is marked checked in the input.
// expected-error@+1 {{the statement 'Prop' is not a proposition}}
schlap.axiom @not_a_statement attributes {checked} {
  %0 = schlap.prop_sort : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

// Recompute the axioms of a theorem that claims to depend on none.
schlap.axiom @em {
  %prop = schlap.prop_sort : !schlap.unknown
  %0 = schlap.forall (%p : !schlap.unknown) in %prop : !schlap.unknown -> !schlap.unknown {
    %1 = schlap.not %p : !schlap.unknown
    %2 = schlap.or %p, %1 : !schlap.unknown
    schlap.yield %2 : !schlap.unknown
  }
  schlap.yield %0 : !schlap.unknown
}

// CHECK-LABEL: schlap.theorem @hidden attributes {axioms = [@em], checked} {
schlap.theorem @hidden attributes {axioms = [], checked} {
  %t = schlap.true : !schlap.unknown
  %0 = schlap.not %t : !schlap.unknown
  %1 = schlap.or %t, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} proof {
  %0 = schlap.ref @em : !schlap.unknown
  %t = schlap.true : !schlap.unknown
  %1 = schlap.app %0, %t : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
}

// CHECK-LABEL: schlap.theorem @uses_hidden attributes {axioms = [@em], checked} {
schlap.theorem @uses_hidden {
  %t = schlap.true : !schlap.unknown
  %0 = schlap.not %t : !schlap.unknown
  %1 = schlap.or %t, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} proof {
  %0 = schlap.ref @hidden : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}
