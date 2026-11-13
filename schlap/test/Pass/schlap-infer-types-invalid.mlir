// RUN: schlap-opt %s -split-input-file -verify-diagnostics -schlap-infer-types

schlap.spec @NotASet {
  %x = schlap.variable "x" : !schlap.unknown
  schlap.init {
    %0 = schlap.int 0 : !schlap.unknown
    %1 = schlap.eq %x, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.int 3 : !schlap.unknown
    // expected-error@+1 {{the right operand of ∈ must be '!schlap.set<!schlap.int>', not '!schlap.int'}}
    %1 = schlap.member %x, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
}

// -----

schlap.spec @NotAFormula {
  %x = schlap.variable "x" : !schlap.unknown
  // expected-error@+1 {{the formula of init must be a formula, not '!schlap.int'}}
  schlap.init {
    %0 = schlap.int 0 : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.true : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}

// -----

schlap.spec @Untyped {
  // expected-error@+1 {{unable to infer the type of variable x}}
  %x = schlap.variable "x" : !schlap.unknown
  schlap.init {
    %0 = schlap.true : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.true : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}

// -----

schlap.spec @TupleIndex {
  %t = schlap.variable "t" : !schlap.tuple<!schlap.int>
  schlap.init {
    // expected-error@+1 {{tuple-ref index 1 is out of range for '!schlap.tuple<!schlap.int>'}}
    %0 = schlap.tuple_get %t[1] : (!schlap.tuple<!schlap.int>) -> !schlap.unknown
    %1 = schlap.true : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.true : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}

// -----

schlap.spec @Foreign {
  %x = schlap.variable "x" : i64
  schlap.init {
    %0 = schlap.int 0 : !schlap.unknown
    // expected-error@+1 {{the operands of = must have one type, not 'i64' and '!schlap.int'}}
    %1 = schlap.eq %x, %0 : (i64, !schlap.unknown) -> !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.true : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}
