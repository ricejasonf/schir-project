// RUN: schlap-opt %s -split-input-file -schlap-normalize-actions \
// RUN:   | FileCheck %s

// Assign from x = e in init and choose from x ∈ S.
// CHECK-LABEL: schlap.spec @Init {
// CHECK-NEXT: [[X:%.+]] = schlap.variable "x"
// CHECK-NEXT: [[Y:%.+]] = schlap.variable "y"
// CHECK-NEXT: schlap.init {
// CHECK-NEXT: [[ZERO:%.+]] = schlap.int 0
// CHECK-NEXT: schlap.assign [[X]], [[ZERO]]
// CHECK-NEXT: [[R:%.+]] = schlap.range [[X]], [[X]]
// CHECK-NEXT: schlap.choose ([[V:%.+]]: !schlap.int) in [[R]]
// CHECK-NEXT: schlap.assign [[Y]], [[V]]
// CHECK-NEXT: }
// CHECK-NEXT: schlap.done
schlap.spec @Init {
  %x = schlap.variable "x" : !schlap.int
  %y = schlap.variable "y" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    %r = schlap.range %x, %x : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
    %2 = schlap.member %y, %r : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
    %3 = schlap.and %1, %2 : !schlap.prop
    schlap.yield %3 : !schlap.prop
  }
  schlap.next {
    %0 = schlap.unchanged %x, %y : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

// Rewrite unchanged as assignments, ∨ as one either, and a formula
// without next values as a guard.
// CHECK-LABEL: schlap.spec @Next {
// CHECK: schlap.next {
// CHECK-NEXT: [[G:%.+]] = schlap.lt [[X:%.+]], [[Y:%.+]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: [[H:%.+]] = schlap.lt [[Y]], [[X]]
// CHECK-NEXT: [[OR:%.+]] = schlap.or [[G]], [[H]]
// CHECK-NEXT: schlap.guard [[OR]] : !schlap.prop
// CHECK-NEXT: schlap.assign [[X]], [[X]]
// CHECK-NEXT: schlap.either {
// CHECK-NEXT: [[P:%.+]] = schlap.prime [[X]]
// CHECK-NEXT: [[ONE:%.+]] = schlap.int 1
// CHECK-NEXT: [[SUM:%.+]] = schlap.add [[P]], [[ONE]]
// CHECK-NEXT: schlap.assign [[Y]], [[SUM]]
// CHECK-NEXT: }, {
// CHECK-NEXT: schlap.assign [[Y]], [[Y]]
// CHECK-NEXT: }, {
// CHECK-NEXT: schlap.assign [[Y]], [[X]]
// CHECK-NEXT: }
// CHECK-NEXT: schlap.done
schlap.spec @Next {
  %x = schlap.variable "x" : !schlap.int
  %y = schlap.variable "y" : !schlap.int
  schlap.init {
    %0 = schlap.eq %x, %y : (!schlap.int, !schlap.int) -> !schlap.prop
    %1 = schlap.int 0 : !schlap.int
    %2 = schlap.eq %y, %1 : (!schlap.int, !schlap.int) -> !schlap.prop
    %3 = schlap.and %2, %0 : !schlap.prop
    schlap.yield %3 : !schlap.prop
  }
  schlap.next {
    %lt = schlap.lt %x, %y : (!schlap.int, !schlap.int) -> !schlap.prop
    %gt = schlap.lt %y, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    %g = schlap.or %lt, %gt : !schlap.prop
    %ux = schlap.unchanged %x : (!schlap.int) -> !schlap.prop
    %px = schlap.prime %x : !schlap.int
    %py = schlap.prime %y : !schlap.int
    %one = schlap.int 1 : !schlap.int
    %sum = schlap.add %px, %one : !schlap.int
    %a = schlap.eq %py, %sum : (!schlap.int, !schlap.int) -> !schlap.prop
    %b = schlap.unchanged %y : (!schlap.int) -> !schlap.prop
    %c = schlap.eq %x, %py : (!schlap.int, !schlap.int) -> !schlap.prop
    %ab = schlap.or %a, %b : !schlap.prop
    %abc = schlap.or %ab, %c : !schlap.prop
    %0 = schlap.and %g, %ux : !schlap.prop
    %1 = schlap.and %0, %abc : !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
}

// -----

// Assign the first (′ x) = e, guard a later one, choose from ∃ and
// (′ x) ∈ S, rewrite ≠ and ∉, and run an action in its own step.
// CHECK-LABEL: schlap.spec @Rules {
// CHECK: schlap.action @Inc {
// CHECK: schlap.next {
// CHECK-NEXT: schlap.in_action @Inc {
// CHECK-NEXT: [[ONE:%.+]] = schlap.int 1
// CHECK-NEXT: [[SUM:%.+]] = schlap.add [[X:%.+]], [[ONE]]
// CHECK-NEXT: schlap.assign [[X]], [[SUM]]
// CHECK-NEXT: [[P:%.+]] = schlap.prime [[X]]
// CHECK-NEXT: [[EQ:%.+]] = schlap.eq [[P]], [[SUM]]
// CHECK-NEXT: schlap.guard [[EQ]]
// CHECK-NEXT: }
// CHECK-NEXT: [[ZERO:%.+]] = schlap.int 0
// CHECK-NEXT: [[S:%.+]] = schlap.range [[ZERO]], [[X]]
// CHECK-NEXT: schlap.choose ([[V:%.+]]: !schlap.int) in [[S]]
// CHECK-NEXT: [[EQ:%.+]] = schlap.eq [[V]], [[X]]
// CHECK-NEXT: [[NE:%.+]] = schlap.not [[EQ]]
// CHECK-NEXT: schlap.guard [[NE]]
// CHECK-NEXT: schlap.choose ([[W:%.+]]: !schlap.int) in [[S]] :
// CHECK-NEXT: schlap.assign [[Y:%.+]], [[W]]
// CHECK-NEXT: }
// CHECK-NEXT: }
// CHECK-NEXT: [[PY:%.+]] = schlap.prime [[Y]]
// CHECK-NEXT: [[M:%.+]] = schlap.member [[PY]], [[S]]
// CHECK-NEXT: [[NM:%.+]] = schlap.not [[M]]
// CHECK-NEXT: schlap.guard [[NM]]
// CHECK-NEXT: schlap.done
// CHECK: schlap.invariant @Inv {
// CHECK-NEXT: [[ZERO:%.+]] = schlap.int 0
// CHECK-NEXT: [[EQ:%.+]] = schlap.eq %{{.+}}, [[ZERO]]
// CHECK-NEXT: [[NE:%.+]] = schlap.not [[EQ]]
// CHECK-NEXT: schlap.yield [[NE]]
schlap.spec @Rules {
  %x = schlap.variable "x" : !schlap.int
  %y = schlap.variable "y" : !schlap.int
  // Copy a definition of the spec body into each region that uses it.
  %zero = schlap.int 0 : !schlap.int
  %s = schlap.range %zero, %x : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
  schlap.init {
    %0 = schlap.eq %x, %zero : (!schlap.int, !schlap.int) -> !schlap.prop
    %1 = schlap.eq %y, %zero : (!schlap.int, !schlap.int) -> !schlap.prop
    %2 = schlap.and %0, %1 : !schlap.prop
    schlap.yield %2 : !schlap.prop
  }
  schlap.action @Inc {
    %px = schlap.prime %x : !schlap.int
    %one = schlap.int 1 : !schlap.int
    %sum = schlap.add %x, %one : !schlap.int
    %0 = schlap.eq %px, %sum : (!schlap.int, !schlap.int) -> !schlap.prop
    %1 = schlap.and %0, %0 : !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.next {
    %inc = schlap.action_ref @Inc : !schlap.prop
    %py = schlap.prime %y : !schlap.int
    %ex = schlap.exists (%v : !schlap.int) in %s : !schlap.set<!schlap.int> -> !schlap.prop {
      %ne = schlap.ne %v, %x : (!schlap.int, !schlap.int) -> !schlap.prop
      %m = schlap.member %py, %s : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
      %a = schlap.and %ne, %m : !schlap.prop
      schlap.yield %a : !schlap.prop
    }
    %nm = schlap.not_member %py, %s : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
    %0 = schlap.and %inc, %ex : !schlap.prop
    %1 = schlap.and %0, %nm : !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.invariant @Inv {
    %0 = schlap.ne %x, %zero : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}

// -----

// Split an ite with next values into two guarded branches.
// CHECK-LABEL: schlap.spec @Ite {
// CHECK: schlap.next {
// CHECK-NEXT: schlap.either {
// CHECK-NEXT: [[C:%.+]] = schlap.lt [[X:%.+]], [[X]]
// CHECK-NEXT: schlap.guard [[C]]
// CHECK-NEXT: [[ONE:%.+]] = schlap.int 1
// CHECK-NEXT: schlap.assign [[X]], [[ONE]]
// CHECK-NEXT: }, {
// CHECK-NEXT: [[C:%.+]] = schlap.lt [[X]], [[X]]
// CHECK-NEXT: [[NC:%.+]] = schlap.not [[C]]
// CHECK-NEXT: schlap.guard [[NC]]
// CHECK-NEXT: [[TWO:%.+]] = schlap.int 2
// CHECK-NEXT: schlap.assign [[X]], [[TWO]]
// CHECK-NEXT: }
// CHECK-NEXT: schlap.done
schlap.spec @Ite {
  %x = schlap.variable "x" : !schlap.int
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.eq %x, %0 : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %1 : !schlap.prop
  }
  schlap.next {
    %c = schlap.lt %x, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    %px = schlap.prime %x : !schlap.int
    %one = schlap.int 1 : !schlap.int
    %two = schlap.int 2 : !schlap.int
    %a = schlap.eq %px, %one : (!schlap.int, !schlap.int) -> !schlap.prop
    %b = schlap.eq %px, %two : (!schlap.int, !schlap.int) -> !schlap.prop
    %0 = schlap.ite %c, %a, %b : (!schlap.prop, !schlap.prop, !schlap.prop) -> !schlap.prop
    schlap.yield %0 : !schlap.prop
  }
}
