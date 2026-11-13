// RUN: schlap-opt %s -split-input-file -verify-diagnostics \
// RUN:   -schlap-normalize-actions

schlap.spec @Unassigned {
  %small = schlap.variable "small" : !schlap.int
  %big = schlap.variable "big" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %small, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    %2 = schlap.eq %big, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    %3 = schlap.and %1, %2 : !schlap.prop
    schlap.yield %3 : !schlap.prop
  }
  // expected-error@+1 {{big has no next value in FillSmall}}
  schlap.action @FillSmall {
    %0 = schlap.int 3 : !schlap.int
    %p = schlap.prime %small : !schlap.int
    %1 = schlap.eq %p, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.action_ref @FillSmall : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

schlap.spec @NoInitialValue {
  %x = schlap.variable "x" : !schlap.int
  // expected-error@+1 {{x has no initial value}}
  schlap.init {
    %0 = schlap.true : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.unchanged %x : (!schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

schlap.spec @ReadEarly {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.next {
    %p = schlap.prime %x : !schlap.int
    // expected-error@+1 {{′ x is read before it is assigned}}
    %0 = schlap.lt %p, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    %1 = schlap.eq %p, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    %2 = schlap.and %0, %1 : !schlap.prop
    schlap.yield %2 : !schlap.prop
  }
}

// -----

schlap.spec @PrimeInInvariant {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.unchanged %x : (!schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.invariant @Bad {
    // expected-error@+1 {{′ is only valid in an action or next}}
    %p = schlap.prime %x : !schlap.int
    %0 = schlap.eq %p, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

schlap.spec @Cycle {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  // expected-error@+1 {{action refers to itself: Loop}}
  schlap.action @Loop {
    %0 = schlap.action_ref @Loop : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.action_ref @Loop : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

schlap.spec @NegatedAction {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.action @Stay {
    %0 = schlap.unchanged %x : (!schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.action_ref @Stay : !schlap.prop
    // expected-error@+1 {{an action is only valid under ∧, ∨, ∃ or ite}}
    %1 = schlap.not %0 : !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
}
