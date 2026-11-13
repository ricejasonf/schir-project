// RUN: schlap-opt %s -schlap-apply-rule | FileCheck %s

// Invalidate the values read from the root of a mutated value and require
// a used value to be valid.
schlap.rule @Invalidate {
  %invalid = schlap.variable "invalid" : !schlap.unknown
  schlap.init {
    %0 = schlap.empty_set : !schlap.unknown
    %1 = schlap.eq %invalid, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.on_mutate {
  ^bb0(%v: !schlap.unknown):
    %0 = schlap.root %v : !schlap.unknown
    %1 = schlap.read_from %0 : !schlap.unknown
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

// Create one action per step, one per region of a branch, a step for a
// mutation, and one invariant per use.
// CHECK-LABEL: schlap.spec @"Invalidate(f)" {
// CHECK-NEXT: [[INVALID:%.+]] = schlap.variable "invalid"
// CHECK-NEXT: [[PC:%.+]] = schlap.variable "pc"
// CHECK-NEXT: schlap.init {
// CHECK-NEXT: [[ZERO:%.+]] = schlap.int 0
// CHECK-NEXT: [[AT:%.+]] = schlap.eq [[PC]], [[ZERO]]
// CHECK-NEXT: [[EMPTY:%.+]] = schlap.empty_set
// CHECK-NEXT: [[INIT:%.+]] = schlap.eq [[INVALID]], [[EMPTY]]
// CHECK-NEXT: [[BOTH:%.+]] = schlap.and [[INIT]], [[AT]]
// CHECK-NEXT: schlap.yield [[BOTH]]
// CHECK: schlap.action @"schlap_test.def #0 defines v0" {
// CHECK: schlap.int 1
// CHECK: schlap.int 0
// CHECK: schlap.unchanged [[INVALID]]
// CHECK: schlap.action @"schlap_test.branch #1 region 0 defines v1" {
// CHECK: schlap.int 2
// CHECK: schlap.int 1
// CHECK: schlap.action @"schlap_test.branch #1 region 1 defines v2" {
// CHECK: schlap.int 4
// CHECK: schlap.int 1
// CHECK: schlap.action @"schlap_test.scope #2" {
// CHECK: schlap.int 3
// CHECK: schlap.int 2
// CHECK: schlap.action @"schlap_test.mutate #3 mutates v1" {
// CHECK: [[NEXT:%.+]] = schlap.prime [[PC]]
// CHECK-NEXT: [[FOUR:%.+]] = schlap.int 4
// CHECK-NEXT: schlap.eq [[NEXT]], [[FOUR]]
// CHECK-NEXT: [[THREE:%.+]] = schlap.int 3
// CHECK-NEXT: schlap.eq [[PC]], [[THREE]]
// CHECK: [[PRIME:%.+]] = schlap.prime [[INVALID]]
// CHECK-NEXT: [[V0:%.+]] = schlap.atom "v0"
// CHECK-NEXT: [[V1:%.+]] = schlap.atom "v1"
// CHECK-NEXT: [[READ:%.+]] = schlap.set_of [[V1]] :
// CHECK-NEXT: [[UNION:%.+]] = schlap.union [[INVALID]], [[READ]]
// CHECK-NEXT: schlap.eq [[PRIME]], [[UNION]]
// CHECK: schlap.action @"schlap_test.use #4" {
// CHECK: schlap.action @"func.return #5" {
// CHECK: schlap.action @end {
// CHECK: schlap.unchanged [[INVALID]], [[PC]]
// CHECK: schlap.int 6
// CHECK: schlap.next {
// CHECK-COUNT-8: schlap.action_ref
// CHECK: schlap.invariant @"use of v0 by schlap_test.branch #1" {
// CHECK-NEXT: [[V0:%.+]] = schlap.atom "v0"
// CHECK-NEXT: [[NOT:%.+]] = schlap.not_member [[V0]], [[INVALID]]
// CHECK-NEXT: [[ONE:%.+]] = schlap.int 1
// CHECK-NEXT: [[AT:%.+]] = schlap.eq [[PC]], [[ONE]]
// CHECK-NEXT: schlap.implies [[AT]], [[NOT]]
// CHECK: schlap.invariant @"use of v1 by schlap_test.mutate #3" {
// CHECK: schlap.invariant @"use of v0 by schlap_test.use #4" {
// CHECK-NOT: schlap.invariant
func.func @f() {
  %a = schlap_test.def : i32
  schlap_test.branch %a : i32 {
  ^bb0(%b: i32):
    schlap_test.scope {
      schlap_test.mutate %b : i32
    }
  }, {
  ^bb0(%c: i32):
  }
  schlap_test.use %a : i32
  return
}
