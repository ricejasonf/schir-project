// RUN: schlap-opt %s -split-input-file -verify-diagnostics \
// RUN:   -schlap-apply-rule

// Reject a root of a value that is not a value of the function.
schlap.rule @RootOfVariable {
  %x = schlap.variable "x" : !schlap.unknown
  schlap.init {
    %0 = schlap.empty_set : !schlap.unknown
    %1 = schlap.eq %x, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.on_use {
  ^bb0(%v: !schlap.unknown):
    // expected-error@+1 {{expects a value of the function}}
    %0 = schlap.root %x : !schlap.unknown
    %1 = schlap.eq %0, %v : !schlap.unknown
    schlap.require %1 : !schlap.unknown
    schlap.done
  }
}

func.func @f() {
  %a = schlap_test.def : i32
  schlap_test.use %a : i32
  return
}

// -----

// Reject a rule with two use events.
schlap.rule @TwoUses {
  schlap.on_use {
  ^bb0(%v: !schlap.unknown):
    schlap.done
  }
  // expected-error@+1 {{expects at most one schlap.on_use}}
  schlap.on_use {
  ^bb0(%v: !schlap.unknown):
    schlap.done
  }
}

func.func @f() {
  return
}

// -----

// Reject a rule variable named pc.
schlap.rule @Pc {
  // expected-error@+1 {{expects a variable not named pc}}
  %pc = schlap.variable "pc" : !schlap.unknown
}

func.func @f() {
  return
}

// -----

// Reject an event without an argument.
schlap.rule @NoArgument {
  // expected-error@+1 {{'schlap.on_mutate' op expects one block argument}}
  schlap.on_mutate {
    schlap.done
  }
}
