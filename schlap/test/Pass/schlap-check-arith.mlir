// RUN: schlap-opt %s -split-input-file -verify-diagnostics \
// RUN:   -schlap-infer-types -schlap-normalize-actions \
// RUN:   -schlap-check="report-states=true"

// Evaluate arith operations in an action through their fold hooks.
// expected-remark@+1 {{spec Mod has 3 distinct states and depth 2}}
schlap.spec @Mod {
  %x = schlap.variable "x" : i64
  schlap.init {
    %0 = arith.constant 0 : i64
    %1 = schlap.eq %x, %0 : (i64, i64) -> !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.action @Inc {
    %one = arith.constant 1 : i64
    %three = arith.constant 3 : i64
    %sum = arith.addi %x, %one : i64
    %rem = arith.remsi %sum, %three : i64
    %px = schlap.prime %x : (i64) -> !schlap.unknown
    %0 = schlap.eq %px, %rem : (!schlap.unknown, i64) -> !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.action_ref @Inc : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  schlap.invariant @Small {
    %three = arith.constant 3 : i64
    %0 = schlap.ne %x, %three : (i64, i64) -> !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}

// -----

// Report a violation found through an arith operation.
schlap.spec @Grow {
  %x = schlap.variable "x" : i64
  // expected-note@+1 {{initial state: x = 1}}
  schlap.init {
    %0 = arith.constant 1 : i64
    %1 = schlap.eq %x, %0 : (i64, i64) -> !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  // expected-note@+2 {{step 1, Double: x = 2}}
  // expected-note@+1 {{step 2, Double: x = 4}}
  schlap.action @Double {
    %two = arith.constant 2 : i64
    %prod = arith.muli %x, %two : i64
    %px = schlap.prime %x : (i64) -> !schlap.unknown
    %0 = schlap.eq %px, %prod : (!schlap.unknown, i64) -> !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.action_ref @Double : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  // expected-error@+1 {{invariant Small is violated after 2 steps}}
  schlap.invariant @Small {
    %four = arith.constant 4 : i64
    %0 = schlap.ne %x, %four : (i64, i64) -> !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}
