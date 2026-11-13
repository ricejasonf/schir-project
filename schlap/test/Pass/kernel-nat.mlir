// RUN: schlap-opt %s -split-input-file -verify-diagnostics -schlap-check-proofs | FileCheck %s

// Unfold a definition and reduce ℕ-rec on zero to prove add n zero = n.
// CHECK-LABEL: schlap.definition @add attributes {axioms = [], checked} : {
// CHECK-LABEL: schlap.theorem @add_zero attributes {axioms = [], checked} {
schlap.definition @add : {
  %nat = schlap.nat : !schlap.unknown
  %0 = schlap.forall (%m : !schlap.unknown) in %nat : !schlap.unknown -> !schlap.unknown {
    %1 = schlap.forall (%n : !schlap.unknown) in %nat : !schlap.unknown -> !schlap.unknown {
      schlap.yield %nat : !schlap.unknown
    }
    schlap.yield %1 : !schlap.unknown
  }
  schlap.yield %0 : !schlap.unknown
} = {
  %0 = schlap.lambda {
  ^bb0(%m: !schlap.unknown):
    %1 = schlap.lambda {
    ^bb0(%n: !schlap.unknown):
      %motive = schlap.lambda {
      ^bb0(%k: !schlap.unknown):
        %nat = schlap.nat : !schlap.unknown
        schlap.yield %nat : !schlap.unknown
      } : !schlap.unknown
      %step = schlap.lambda {
      ^bb0(%k: !schlap.unknown):
        %2 = schlap.lambda {
        ^bb0(%r: !schlap.unknown):
          %3 = schlap.succ %r : !schlap.unknown
          schlap.yield %3 : !schlap.unknown
        } : !schlap.unknown
        schlap.yield %2 : !schlap.unknown
      } : !schlap.unknown
      %4 = schlap.nat_rec %motive, %m, %step, %n : (!schlap.unknown, !schlap.unknown, !schlap.unknown, !schlap.unknown) -> !schlap.unknown
      schlap.yield %4 : !schlap.unknown
    } : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  } : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

schlap.theorem @add_zero {
  %nat = schlap.nat : !schlap.unknown
  %0 = schlap.forall (%n : !schlap.unknown) in %nat : !schlap.unknown -> !schlap.unknown {
    %add = schlap.ref @add : !schlap.unknown
    %zero = schlap.zero : !schlap.unknown
    %1 = schlap.app %add, %n : !schlap.unknown
    %2 = schlap.app %1, %zero : !schlap.unknown
    %3 = schlap.eq %2, %n : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  schlap.yield %0 : !schlap.unknown
} proof {
  %0 = schlap.lambda {
  ^bb0(%n: !schlap.unknown):
    %1 = schlap.refl : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  } : !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}

// -----

// Reject ℕ-rec whose step does not have the type of the motive.
schlap.definition @bad_rec : {
  %nat = schlap.nat : !schlap.unknown
  schlap.yield %nat : !schlap.unknown
} = {
  %motive = schlap.lambda {
  ^bb0(%k: !schlap.unknown):
    %nat = schlap.nat : !schlap.unknown
    schlap.yield %nat : !schlap.unknown
  } : !schlap.unknown
  %zero = schlap.zero : !schlap.unknown
  // expected-error@+1 {{expected 'ℕ ⇒ ℕ ⇒ ℕ' but got 'ℕ'}}
  %step = schlap.zero : !schlap.unknown
  %0 = schlap.nat_rec %motive, %zero, %step, %zero : (!schlap.unknown, !schlap.unknown, !schlap.unknown, !schlap.unknown) -> !schlap.unknown
  schlap.yield %0 : !schlap.unknown
}
