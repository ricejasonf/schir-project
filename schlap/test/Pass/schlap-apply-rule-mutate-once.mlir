// RUN: schlap-opt %s -verify-diagnostics -schlap-apply-rule \
// RUN:   -schlap-infer-types -schlap-normalize-actions \
// RUN:   -schlap-check="report-states=true"

// Require each value to be mutated at most once on every path.
schlap.rule @MutateOnce {
  %mutated = schlap.variable "mutated" : !schlap.unknown
  schlap.init {
    %0 = schlap.empty_set : !schlap.unknown
    %1 = schlap.eq %mutated, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.on_mutate {
  ^bb0(%v: !schlap.unknown):
    %0 = schlap.not_member %v, %mutated : !schlap.unknown
    schlap.require %0 : !schlap.unknown
    %1 = schlap.set_of %v : (!schlap.unknown) -> !schlap.unknown
    %2 = schlap.union %mutated, %1 : !schlap.unknown
    %3 = schlap.prime %mutated : !schlap.unknown
    %4 = schlap.eq %3, %2 : !schlap.unknown
    schlap.yield %4 : !schlap.unknown
  }
}

// Accept one mutation in each branch.
// expected-remark@+1 {{spec MutateOnce(branches) has 6 distinct states and depth 4}}
func.func @branches() {
  %a = schlap_test.def : i32
  schlap_test.branch %a : i32 {
  ^bb0(%b: i32):
    schlap_test.mutate %a : i32
  }, {
  ^bb0(%c: i32):
    schlap_test.mutate %a : i32
  }
  return
}

// Report a second mutation after a nested region.
// expected-note@+1 {{initial state: mutated = {}, pc = 0}}
func.func @twice() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: mutated = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+1 {{step 2, schlap_test.scope #1: mutated = {}, pc = 2}}
  schlap_test.scope {
    // expected-note@+1 {{step 3, schlap_test.mutate #2 mutates v0: mutated = {v0}, pc = 3}}
    schlap_test.mutate %a : i32
  }
  // expected-error@+1 {{invariant mutation of v0 by schlap_test.mutate #3 is violated after 3 steps}}
  schlap_test.mutate %a : i32
  return
}

// Report a value mutated twice by one operation.
// expected-note@+1 {{initial state: mutated = {}, pc = 0}}
func.func @same_operation() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: mutated = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+2 {{step 2, schlap_test.mutate #1 mutates v0: mutated = {v0}, pc = 2}}
  // expected-error@+1 {{invariant mutation of v0 by schlap_test.mutate #1 (2) is violated after 2 steps}}
  schlap_test.mutate %a, %a : i32, i32
  return
}
