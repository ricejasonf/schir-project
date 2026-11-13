// RUN: schlap-opt %s -schlap-check-proofs | FileCheck %s

// Rewrite ¬ and ⇔ to Π, check the proof and set the skeleton types.
// CHECK-LABEL: schlap.theorem @not_iff attributes {axioms = [], checked} {
// CHECK-NEXT: %[[PROP:.+]] = schlap.prop_sort : !schlap.term
// CHECK-NEXT: schlap.pi (%[[P:.+]]: !schlap.prop) in %[[PROP]] : !schlap.term -> !schlap.prop {
// CHECK-NEXT: %[[F:.+]] = schlap.false : !schlap.prop
// CHECK-NEXT: %[[NOT:.+]] = schlap.pi (%{{.+}}: !schlap.proof) in %[[P]] : !schlap.prop -> !schlap.prop {
// CHECK-NEXT: schlap.yield %[[F]] : !schlap.prop
// CHECK-NEXT: }
// CHECK-NEXT: %[[TO:.+]] = schlap.pi (%{{.+}}: !schlap.proof) in %[[NOT]] : !schlap.prop -> !schlap.prop {
// CHECK-NEXT: schlap.yield %[[NOT]] : !schlap.prop
// CHECK-NEXT: }
// CHECK-NEXT: %[[FROM:.+]] = schlap.pi (%{{.+}}: !schlap.proof) in %[[NOT]] : !schlap.prop -> !schlap.prop {
// CHECK-NEXT: schlap.yield %[[NOT]] : !schlap.prop
// CHECK-NEXT: }
// CHECK-NEXT: %[[AND:.+]] = schlap.and %[[TO]], %[[FROM]] : !schlap.prop
// CHECK-NEXT: schlap.yield %[[AND]] : !schlap.prop
schlap.theorem @not_iff {
  %prop = schlap.prop_sort : !schlap.unknown
  %0 = schlap.forall (%p : !schlap.unknown) in %prop : !schlap.unknown -> !schlap.unknown {
    %1 = schlap.not %p : !schlap.unknown
    %2 = schlap.iff %1, %1 : !schlap.unknown
    schlap.yield %2 : !schlap.unknown
  }
  schlap.yield %0 : !schlap.unknown
} proof {
  %0 = schlap.lambda {
  ^bb0(%p: !schlap.unknown):
    %id = schlap.lambda {
    ^bb0(%h: !schlap.unknown):
      schlap.yield %h : !schlap.unknown
    } : !schlap.unknown
    %1 = schlap.and_intro %id, %id : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  } : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// Use an earlier theorem.
// CHECK-LABEL: schlap.theorem @use attributes {axioms = [], checked} {
// CHECK: } proof {
// CHECK-NEXT: %[[REF:.+]] = schlap.ref @not_iff : !schlap.proof
// CHECK-NEXT: %[[T:.+]] = schlap.true : !schlap.prop
// CHECK-NEXT: %[[APP:.+]] = schlap.app %[[REF]], %[[T]] : (!schlap.proof, !schlap.prop) -> !schlap.proof
schlap.theorem @use {
  %t = schlap.true : !schlap.unknown
  %0 = schlap.not %t : !schlap.unknown
  %1 = schlap.iff %0, %0 : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
} proof {
  %0 = schlap.ref @not_iff : !schlap.unknown
  %t = schlap.true : !schlap.unknown
  %1 = schlap.app %0, %t : !schlap.unknown
  schlap.yield %1 : !schlap.unknown
}
