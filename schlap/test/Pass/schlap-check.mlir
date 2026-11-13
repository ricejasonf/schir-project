// RUN: schlap-opt %s -split-input-file -verify-diagnostics \
// RUN:   -schlap-check="report-states=true state-limit=10"

// Count the states of a spec whose invariant holds.
// expected-remark@+1 {{spec Wrap has 3 distinct states and depth 2}}
schlap.spec @Wrap {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    schlap.assign %x, %0 : !schlap.int
    schlap.done
  }
  schlap.next {
    %0 = schlap.int 1 : !schlap.int
    %1 = schlap.int 3 : !schlap.int
    %2 = schlap.add %x, %0 : !schlap.int
    %3 = schlap.lt %2, %1 : (!schlap.int, !schlap.int) -> !schlap.prop
    %4 = schlap.int 0 : !schlap.int
    %5 = schlap.ite %3, %2, %4 : (!schlap.prop, !schlap.int, !schlap.int) -> !schlap.int
    schlap.assign %x, %5 : !schlap.int
    schlap.done
  }
  schlap.invariant @Small {
    %0 = schlap.int 3 : !schlap.int
    %1 = schlap.lt %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
}

// -----

// Report a state with no next state at the spec.
// expected-error@+1 {{spec Countdown deadlocks after 1 step}}
schlap.spec @Countdown {
  %x = schlap.variable "x" : !schlap.int
  // expected-note@+1 {{initial state: x = 1}}
  schlap.init {
    %0 = schlap.int 1 : !schlap.int
    schlap.assign %x, %0 : !schlap.int
    schlap.done
  }
  // expected-note@+1 {{step 1, Dec: x = 0}}
  schlap.action @Dec {
    %0 = schlap.true : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.next {
    schlap.in_action @Dec {
      %0 = schlap.int 0 : !schlap.int
      %1 = schlap.lt %0, %x : (!schlap.int, !schlap.int) -> !schlap.prop
      schlap.guard %1 : !schlap.prop
      %2 = schlap.int 1 : !schlap.int
      %3 = schlap.sub %x, %2 : !schlap.int
      schlap.assign %x, %3 : !schlap.int
    }
    schlap.done
  }
}

// -----

// Stop at the state limit.
// expected-error@+1 {{spec Counter exceeds the state limit of 10 states}}
schlap.spec @Counter {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    schlap.assign %x, %0 : !schlap.int
    schlap.done
  }
  schlap.next {
    %0 = schlap.int 1 : !schlap.int
    %1 = schlap.add %x, %0 : !schlap.int
    schlap.assign %x, %1 : !schlap.int
    schlap.done
  }
}

// -----

// Report integer overflow at the operation.
schlap.spec @Overflow {
  %x = schlap.variable "x" : !schlap.int
  // expected-note@+1 {{initial state: x = 9223372036854775807}}
  schlap.init {
    %0 = schlap.int 9223372036854775807 : !schlap.int
    schlap.assign %x, %0 : !schlap.int
    schlap.done
  }
  schlap.next {
    %0 = schlap.int 1 : !schlap.int
    // expected-error@+1 {{integer overflow}}
    %1 = schlap.add %x, %0 : !schlap.int
    schlap.assign %x, %1 : !schlap.int
    schlap.done
  }
}

// -----

// Choose initial states from a set and stop at the first violation.
schlap.spec @Choose {
  %x = schlap.variable "x" : !schlap.int
  // expected-note@+1 {{initial state: x = 0}}
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.int 2 : !schlap.int
    %2 = schlap.range %0, %1 : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
    schlap.choose (%v : !schlap.int) in %2 : !schlap.set<!schlap.int> {
      schlap.assign %x, %v : !schlap.int
    }
    schlap.done
  }
  schlap.next {
    schlap.assign %x, %x : !schlap.int
    schlap.done
  }
  // expected-error@+1 {{invariant Positive is violated after 0 steps}}
  schlap.invariant @Positive {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.lt %0, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
}

// -----

// Require a normalized spec.
schlap.spec @Formula {
  %x = schlap.variable "x" : !schlap.int
  // expected-error@+1 {{expects a normalized specification (run schlap-normalize-actions)}}
  schlap.init {
    %0 = schlap.true : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
  schlap.next {
    schlap.done
  }
}
