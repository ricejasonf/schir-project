// RUN: schlap-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'schlap.spec' op expects exactly one schlap.next}}
schlap.spec @NoNext {
  schlap.init {
    %0 = schlap.true : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

schlap.spec @PrimeOfValue {
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    // expected-error@+1 {{'schlap.prime' op expects a state variable}}
    %1 = schlap.prime %0 : !schlap.int
    %2 = schlap.true : !schlap.prop
    schlap.yield %2 : !schlap.prop
  }
  schlap.next {
    schlap.done
  }
}

// -----

schlap.spec @AssignValue {
  schlap.init {
    schlap.done
  }
  schlap.next {
    %0 = schlap.int 0 : !schlap.int
    // expected-error@+1 {{'schlap.assign' op expects a state variable}}
    schlap.assign %0, %0 : !schlap.int
    schlap.done
  }
}

// -----

schlap.spec @UnknownAction {
  schlap.init {
    schlap.done
  }
  schlap.next {
    // expected-error@+1 {{'schlap.action_ref' op expects an action: @Missing}}
    %0 = schlap.action_ref @Missing : !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

// expected-error@+1 {{'schlap.variable' op expects parent op to be one of 'schlap.spec, schlap.rule'}}
%0 = schlap.variable "x" : !schlap.int
