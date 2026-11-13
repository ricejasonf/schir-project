// RUN: schlap-opt %s | FileCheck %s
// RUN: schlap-opt %s | schlap-opt | FileCheck %s

// CHECK-LABEL: schlap.spec @Ops {
// CHECK-NEXT: [[X:%.+]] = schlap.variable "x" : !schlap.int
// CHECK-NEXT: [[F:%.+]] = schlap.variable "f" : !schlap.fn<!schlap.int, !schlap.atom>
// CHECK-NEXT: [[T:%.+]] = schlap.variable "t" : !schlap.tuple<!schlap.int, !schlap.prop>
// CHECK-NEXT: schlap.init {
// CHECK: [[S:%.+]] = schlap.set_of {{.+}} : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
// CHECK-NEXT: [[E:%.+]] = schlap.set_of : () -> !schlap.set<!schlap.int>
// CHECK-NEXT: [[FN:%.+]] = schlap.fn ({{%.+}}: !schlap.int) in [[S]] : !schlap.set<!schlap.int> -> !schlap.fn<!schlap.int, !schlap.atom> {
// CHECK-NEXT: [[A:%.+]] = schlap.atom "idle" : !schlap.atom
// CHECK-NEXT: schlap.yield [[A]] : !schlap.atom
// CHECK-NEXT: }
// CHECK: [[TU:%.+]] = schlap.tuple {{.+}} : (!schlap.int, !schlap.prop) -> !schlap.tuple<!schlap.int, !schlap.prop>
// CHECK: schlap.yield
// CHECK: schlap.action @Step {
// CHECK-NEXT: [[PX:%.+]] = schlap.prime [[X]] : !schlap.int
// CHECK-NEXT: [[U:%.+]] = schlap.unchanged [[F]], [[T]] : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.tuple<!schlap.int, !schlap.prop>) -> !schlap.prop
// CHECK-NEXT: [[G:%.+]] = schlap.tuple_get [[T]][1] : (!schlap.tuple<!schlap.int, !schlap.prop>) -> !schlap.prop
// CHECK-NEXT: [[I:%.+]] = schlap.ite [[G]], [[X]], [[PX]] : (!schlap.prop, !schlap.int, !schlap.int) -> !schlap.int
// CHECK-NEXT: [[AP:%.+]] = schlap.apply [[F]], [[I]] : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.int) -> !schlap.atom
// CHECK-NEXT: [[UP:%.+]] = schlap.update [[F]], [[I]], [[AP]] : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.int, !schlap.atom) -> !schlap.fn<!schlap.int, !schlap.atom>
// CHECK: schlap.next {
// CHECK-NEXT: schlap.either {
// CHECK-NEXT: schlap.in_action @Step {
// CHECK-NEXT: [[ONE:%.+]] = schlap.int 1 : !schlap.int
// CHECK-NEXT: schlap.assign [[X]], [[ONE]] : !schlap.int
// CHECK-NEXT: }
// CHECK-NEXT: }, {
// CHECK-NEXT: [[S2:%.+]] = schlap.set_of
// CHECK-NEXT: schlap.choose ([[V:%.+]]: !schlap.int) in [[S2]] : !schlap.set<!schlap.int> {
// CHECK-NEXT: schlap.assign [[X]], [[V]] : !schlap.int
// CHECK-NEXT: }
// CHECK-NEXT: [[TRUE:%.+]] = schlap.true : !schlap.prop
// CHECK-NEXT: schlap.guard [[TRUE]] : !schlap.prop
// CHECK-NEXT: }
// CHECK-NEXT: schlap.done
// CHECK-NEXT: }
// CHECK-NEXT: schlap.invariant @Inv {
schlap.spec @Ops {
  %x = schlap.variable "x" : !schlap.int
  %f = schlap.variable "f" : !schlap.fn<!schlap.int, !schlap.atom>
  %t = schlap.variable "t" : !schlap.tuple<!schlap.int, !schlap.prop>
  schlap.init {
    %0 = schlap.int 0 : !schlap.int
    %1 = schlap.int 1 : !schlap.int
    %s = schlap.set_of %0, %1 : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
    %e = schlap.set_of : () -> !schlap.set<!schlap.int>
    %fn = schlap.fn (%p : !schlap.int) in %s
        : !schlap.set<!schlap.int> -> !schlap.fn<!schlap.int, !schlap.atom> {
      %a = schlap.atom "idle" : !schlap.atom
      schlap.yield %a : !schlap.atom
    }
    %true = schlap.true : !schlap.prop
    %tu = schlap.tuple %0, %true : (!schlap.int, !schlap.prop) -> !schlap.tuple<!schlap.int, !schlap.prop>
    schlap.yield %true : !schlap.prop
  }
  schlap.action @Step {
    %px = schlap.prime %x : !schlap.int
    %u = schlap.unchanged %f, %t : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.tuple<!schlap.int, !schlap.prop>) -> !schlap.prop
    %g = schlap.tuple_get %t[1] : (!schlap.tuple<!schlap.int, !schlap.prop>) -> !schlap.prop
    %i = schlap.ite %g, %x, %px : (!schlap.prop, !schlap.int, !schlap.int) -> !schlap.int
    %ap = schlap.apply %f, %i : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.int) -> !schlap.atom
    %up = schlap.update %f, %i, %ap : (!schlap.fn<!schlap.int, !schlap.atom>, !schlap.int, !schlap.atom) -> !schlap.fn<!schlap.int, !schlap.atom>
    schlap.yield %u : !schlap.prop
  }
  schlap.next {
    schlap.either {
      schlap.in_action @Step {
        %one = schlap.int 1 : !schlap.int
        schlap.assign %x, %one : !schlap.int
      }
    }, {
      %s = schlap.set_of : () -> !schlap.set<!schlap.int>
      schlap.choose (%v : !schlap.int) in %s : !schlap.set<!schlap.int> {
        schlap.assign %x, %v : !schlap.int
      }
      %true = schlap.true : !schlap.prop
      schlap.guard %true : !schlap.prop
    }
    schlap.done
  }
  schlap.invariant @Inv {
    %true = schlap.true : !schlap.prop
    schlap.yield %true : !schlap.prop
  }
}
