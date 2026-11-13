// RUN: schlap-opt %s -schlap-infer-types | FileCheck %s

// CHECK-LABEL: schlap.spec @Infer {
// CHECK-NEXT: [[X:%.+]] = schlap.variable "x" : !schlap.int
// CHECK-NEXT: [[S:%.+]] = schlap.variable "s" : !schlap.set<!schlap.atom>
// CHECK-NEXT: [[F:%.+]] = schlap.variable "f" : !schlap.fn<!schlap.int, !schlap.prop>
// CHECK-NEXT: [[T:%.+]] = schlap.variable "t" : !schlap.tuple<!schlap.int, !schlap.atom>
// CHECK-NEXT: [[B:%.+]] = schlap.variable "b" : !schlap.prop
// CHECK-NEXT: schlap.init {
// CHECK-NEXT: schlap.int 0 : !schlap.int
// CHECK-NEXT: schlap.eq [[X]], %{{.+}} : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: schlap.empty_set : !schlap.set<!schlap.atom>
// CHECK-NEXT: schlap.eq [[S]], %{{.+}} : (!schlap.set<!schlap.atom>, !schlap.set<!schlap.atom>) -> !schlap.prop
// CHECK-NEXT: [[R:%.+]] = schlap.range %{{.+}}, %{{.+}} : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
// CHECK-NEXT: schlap.fn ({{%.+}}: !schlap.int) in [[R]] : !schlap.set<!schlap.int> -> !schlap.fn<!schlap.int, !schlap.prop> {
// CHECK-NEXT: schlap.false : !schlap.prop
// CHECK-NEXT: schlap.yield %{{.+}} : !schlap.prop
// CHECK: schlap.tuple %{{.+}}, %{{.+}} : (!schlap.int, !schlap.atom) -> !schlap.tuple<!schlap.int, !schlap.atom>
// CHECK: schlap.member [[B]], %{{.+}} : (!schlap.prop, !schlap.set<!schlap.prop>) -> !schlap.prop
// CHECK: schlap.yield %{{.+}} : !schlap.prop
// CHECK: schlap.action @Step {
// CHECK-NEXT: schlap.prime [[S]] : !schlap.set<!schlap.atom>
// CHECK-NEXT: schlap.atom "a" : !schlap.atom
// CHECK-NEXT: schlap.set_of %{{.+}} : (!schlap.atom) -> !schlap.set<!schlap.atom>
// CHECK-NEXT: schlap.union %{{.+}}, %{{.+}} : !schlap.set<!schlap.atom>
// CHECK-NEXT: schlap.eq
// CHECK-NEXT: schlap.apply [[F]], [[X]] : (!schlap.fn<!schlap.int, !schlap.prop>, !schlap.int) -> !schlap.prop
// CHECK-NEXT: schlap.tuple_get [[T]][1] : (!schlap.tuple<!schlap.int, !schlap.atom>) -> !schlap.atom
// CHECK-NEXT: schlap.ite %{{.+}}, %{{.+}}, %{{.+}} : (!schlap.prop, !schlap.atom, !schlap.atom) -> !schlap.atom
// CHECK-NEXT: schlap.not_member %{{.+}}, [[S]] : (!schlap.atom, !schlap.set<!schlap.atom>) -> !schlap.prop
// CHECK-NEXT: schlap.exists ({{%.+}}: !schlap.atom) in [[S]] : !schlap.set<!schlap.atom> -> !schlap.prop {
// CHECK: schlap.action_ref @Step : !schlap.prop
schlap.spec @Infer {
  %x = schlap.variable "x" : !schlap.unknown
  %s = schlap.variable "s" : !schlap.unknown
  %f = schlap.variable "f" : !schlap.unknown
  %t = schlap.variable "t" : !schlap.unknown
  %b = schlap.variable "b" : !schlap.unknown
  schlap.init {
    %0 = schlap.int 0 : !schlap.unknown
    %1 = schlap.eq %x, %0 : !schlap.unknown
    %2 = schlap.empty_set : !schlap.unknown
    %3 = schlap.eq %s, %2 : !schlap.unknown
    %r = schlap.range %0, %0 : !schlap.unknown
    %fn = schlap.fn (%p : !schlap.unknown) in %r : !schlap.unknown -> !schlap.unknown {
      %no = schlap.false : !schlap.unknown
      schlap.yield %no : !schlap.unknown
    }
    %4 = schlap.eq %f, %fn : !schlap.unknown
    %a = schlap.atom "a" : !schlap.unknown
    %tu = schlap.tuple %0, %a : (!schlap.unknown, !schlap.unknown) -> !schlap.unknown
    %5 = schlap.eq %t, %tu : !schlap.unknown
    %true = schlap.true : !schlap.unknown
    %bs = schlap.set_of %true : (!schlap.unknown) -> !schlap.unknown
    %6 = schlap.member %b, %bs : !schlap.unknown
    %7 = schlap.and %1, %3 : !schlap.unknown
    %8 = schlap.and %7, %4 : !schlap.unknown
    %9 = schlap.and %8, %5 : !schlap.unknown
    %10 = schlap.and %9, %6 : !schlap.unknown
    schlap.yield %10 : !schlap.unknown
  }
  schlap.action @Step {
    %ps = schlap.prime %s : !schlap.unknown
    %a = schlap.atom "a" : !schlap.unknown
    %sa = schlap.set_of %a : (!schlap.unknown) -> !schlap.unknown
    %u = schlap.union %s, %sa : !schlap.unknown
    %0 = schlap.eq %ps, %u : !schlap.unknown
    %ap = schlap.apply %f, %x : !schlap.unknown
    %g = schlap.tuple_get %t[1] : !schlap.unknown
    %i = schlap.ite %ap, %g, %a : !schlap.unknown
    %nm = schlap.not_member %i, %s : !schlap.unknown
    %ex = schlap.exists (%e : !schlap.unknown) in %s : !schlap.unknown -> !schlap.unknown {
      %eq = schlap.eq %e, %i : !schlap.unknown
      schlap.yield %eq : !schlap.unknown
    }
    %1 = schlap.and %0, %nm : !schlap.unknown
    %2 = schlap.and %1, %ex : !schlap.unknown
    schlap.yield %2 : !schlap.unknown
  }
  schlap.next {
    %0 = schlap.action_ref @Step : !schlap.unknown
    schlap.yield %0 : !schlap.unknown
  }
}
