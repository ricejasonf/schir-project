// RUN: schlap-opt %s -verify-diagnostics -schlap-apply-rule \
// RUN:   -schlap-infer-types -schlap-normalize-actions \
// RUN:   -schlap-check="report-states=true"

// Invalidate the values that a mutated value may own and require a mutated
// or used value to be valid.
schlap.rule @Invalidate {
  %invalid = schlap.variable "invalid" : !schlap.unknown
  schlap.init {
    %0 = schlap.empty_set : !schlap.unknown
    %1 = schlap.eq %invalid, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.on_mutate {
  ^bb0(%v: !schlap.unknown):
    %0 = schlap.not_member %v, %invalid : !schlap.unknown
    schlap.require %0 : !schlap.unknown
    %1 = schlap.owned_by %v : !schlap.unknown
    %2 = schlap.union %invalid, %1 : !schlap.unknown
    %3 = schlap.prime %invalid : !schlap.unknown
    %4 = schlap.eq %3, %2 : !schlap.unknown
    schlap.yield %4 : !schlap.unknown
  }
  schlap.on_use {
  ^bb0(%v: !schlap.unknown):
    %0 = schlap.not_member %v, %invalid : !schlap.unknown
    schlap.require %0 : !schlap.unknown
    schlap.done
  }
}

// Report a use of a value read from a root after a mutation of the root.
// expected-note@+1 {{initial state: invalid = {}, pc = 0}}
func.func @use_after_mutate() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: invalid = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+1 {{step 2, schlap_test.read #1 defines v1: invalid = {}, pc = 2}}
  %b = schlap_test.read %a : i32 -> i32
  // expected-note@+1 {{step 3, schlap_test.scope #2: invalid = {}, pc = 3}}
  schlap_test.scope {
    // expected-note@+2 {{step 4, schlap_test.mutate #3 mutates v0: invalid = {v1}, pc = 4}}
    // expected-note@+1 {{step 5, schlap_test.mutate #3: invalid = {v1}, pc = 5}}
    schlap_test.mutate %a : i32
  }
  // expected-error@+1 {{invariant use of v1 by schlap_test.use #4 is violated after 5 steps}}
  schlap_test.use %b : i32
  return
}

// Accept a value read from the root again after the mutation.
// expected-remark@+1 {{spec Invalidate(read_again) has 8 distinct states and depth 7}}
func.func @read_again() {
  %a = schlap_test.def : i32
  %b = schlap_test.read %a : i32 -> i32
  schlap_test.mutate %a : i32
  %c = schlap_test.read %a : i32 -> i32
  schlap_test.use %a, %c : i32, i32
  return
}

// Accept a value read from another root.
// expected-remark@+1 {{spec Invalidate(other_root) has 8 distinct states and depth 7}}
func.func @other_root() {
  %a = schlap_test.def : i32
  %b = schlap_test.def : i32
  %c = schlap_test.read %a : i32 -> i32
  schlap_test.mutate %b : i32
  schlap_test.use %c : i32
  return
}

// Accept an operand that is not used.
// expected-remark@+1 {{spec Invalidate(forget) has 7 distinct states and depth 6}}
func.func @forget() {
  %a = schlap_test.def : i32
  %b = schlap_test.read %a : i32 -> i32
  schlap_test.mutate %a : i32
  schlap_test.forget %b : i32
  return
}

// Accept a use of a mutated value and of the value it is read from.
// expected-remark@+1 {{spec Invalidate(use_mutated) has 7 distinct states and depth 6}}
func.func @use_mutated() {
  %a = schlap_test.def : i32
  %b = schlap_test.read %a {key = "x"} : i32 -> i32
  schlap_test.mutate %b : i32
  schlap_test.use %a, %b : i32, i32
  return
}

// Report a mutation of a value read from a value that the same operation
// mutates first.
// expected-note@+1 {{initial state: invalid = {}, pc = 0}}
func.func @same_operation() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: invalid = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+1 {{step 2, schlap_test.read #1 defines v1: invalid = {}, pc = 2}}
  %b = schlap_test.read %a {key = "x"} : i32 -> i32
  // expected-note@+2 {{step 3, schlap_test.mutate #2 mutates v0: invalid = {v1}, pc = 3}}
  // expected-error@+1 {{invariant mutation of v1 by schlap_test.mutate #2 is violated after 3 steps}}
  schlap_test.mutate %a, %b : i32, i32
  return
}

// Report a use after the branch that mutates, and accept the use in the
// other branch.
// expected-note@+1 {{initial state: invalid = {}, pc = 0}}
func.func @branch() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: invalid = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+1 {{step 2, schlap_test.read #1 defines v1: invalid = {}, pc = 2}}
  %b = schlap_test.read %a : i32 -> i32
  // expected-note@+1 {{step 3, schlap_test.branch #2 region 0 defines v2: invalid = {}, pc = 3}}
  schlap_test.branch %a : i32 {
  ^bb0(%c: i32):
    // expected-note@+2 {{step 4, schlap_test.mutate #3 mutates v0: invalid = {v1, v2}, pc = 4}}
    // expected-note@+1 {{step 5, schlap_test.mutate #3: invalid = {v1, v2}, pc = 6}}
    schlap_test.mutate %a : i32
  }, {
  ^bb0(%d: i32):
    schlap_test.use %d : i32
  }
  // expected-error@+1 {{invariant use of v1 by schlap_test.use #5 is violated after 5 steps}}
  schlap_test.use %b : i32
  return
}

// Report a use of the argument of an enclosing branch after a mutation of
// the root in a nested branch.
// expected-note@+1 {{initial state: invalid = {}, pc = 0}}
func.func @nested() {
  // expected-note@+1 {{step 1, schlap_test.def #0 defines v0: invalid = {}, pc = 1}}
  %a = schlap_test.def : i32
  // expected-note@+1 {{step 2, schlap_test.branch #1 defines v1: invalid = {}, pc = 2}}
  schlap_test.branch %a : i32 {
  ^bb0(%x: i32):
    // expected-note@+1 {{step 3, schlap_test.scope #2: invalid = {}, pc = 3}}
    schlap_test.scope {
      // expected-note@+1 {{step 4, schlap_test.read #3 defines v2: invalid = {}, pc = 4}}
      %y = schlap_test.read %a : i32 -> i32
      // expected-note@+1 {{step 5, schlap_test.branch #4 region 0 defines v3: invalid = {}, pc = 5}}
      schlap_test.branch %y : i32 {
      ^bb0(%z: i32):
        // expected-note@+2 {{step 6, schlap_test.mutate #5 mutates v0: invalid = {v1, v2, v3}, pc = 6}}
        // expected-note@+1 {{step 7, schlap_test.mutate #5: invalid = {v1, v2, v3}, pc = 7}}
        schlap_test.mutate %a : i32
      }, {
      ^bb0(%w: i32):
      }
    }
    // expected-error@+1 {{invariant use of v1 by schlap_test.use #6 is violated after 7 steps}}
    schlap_test.use %x : i32
  }
  return
}
