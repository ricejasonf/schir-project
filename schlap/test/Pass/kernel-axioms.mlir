// RUN: schlap-opt %s -schlap-check-proofs | FileCheck %s

// List the axioms a theorem depends on, also through other theorems.
// CHECK-LABEL: schlap.axiom @em attributes {checked} {
schlap.axiom @em {
  %prop = schlap.prop_sort : !schlap.unknown
  %0 = schlap.forall (%p : !schlap.unknown) in %prop : !schlap.unknown -> !schlap.unknown {
    %1 = schlap.not %p : !schlap.unknown
    %2 = schlap.or %p, %1 : !schlap.unknown
    schlap.yield %2 : !schlap.unknown
  }
  schlap.yield %0 : !schlap.unknown
}

// CHECK-LABEL: schlap.axiom @other attributes {checked} {
schlap.axiom @other {
  %0 = schlap.true : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// CHECK-LABEL: schlap.theorem @em_true attributes {axioms = [@em], checked} {
schlap.theorem @em_true {
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

// CHECK-LABEL: schlap.theorem @both attributes {axioms = [@em, @other], checked} {
schlap.theorem @both {
  %t = schlap.true : !schlap.unknown
  %0 = schlap.not %t : !schlap.unknown
  %1 = schlap.or %t, %0 : !schlap.unknown
  %2 = schlap.and %1, %t : !schlap.unknown
  schlap.yield %2 : !schlap.unknown
} proof {
  %0 = schlap.ref @em_true : !schlap.unknown
  %1 = schlap.ref @other : !schlap.unknown
  %2 = schlap.and_intro %0, %1 : !schlap.unknown
  schlap.yield %2 : !schlap.unknown
}
