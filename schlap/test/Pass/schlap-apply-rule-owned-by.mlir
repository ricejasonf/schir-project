// RUN: schlap-opt %s -schlap-apply-rule | FileCheck %s

// Invalidate the values that a mutated value may own.
schlap.rule @InvalidateOwned {
  %invalid = schlap.variable "invalid" : !schlap.unknown
  schlap.init {
    %0 = schlap.empty_set : !schlap.unknown
    %1 = schlap.eq %invalid, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
  schlap.on_mutate {
  ^bb0(%v: !schlap.unknown):
    %0 = schlap.owned_by %v : !schlap.unknown
    %1 = schlap.union %invalid, %0 : !schlap.unknown
    %2 = schlap.prime %invalid : !schlap.unknown
    %3 = schlap.eq %2, %1 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
}

// Own the values read with keys extending the key of the mutated value,
// including those read from its twin, and fall back to the root for an
// unkeyed read.
// CHECK-LABEL: schlap.spec @"InvalidateOwned(twins)"
// CHECK: schlap.action @"schlap_test.mutate #7 mutates v1" {
// CHECK: [[V3:%.+]] = schlap.atom "v3"
// CHECK-NEXT: [[V4:%.+]] = schlap.atom "v4"
// CHECK-NEXT: [[V6:%.+]] = schlap.atom "v6"
// CHECK-NEXT: schlap.set_of [[V3]], [[V4]], [[V6]] :
func.func @twins() {
  %r = schlap_test.def : i32
  %a1 = schlap_test.read %r {key = "a"} : i32 -> i32
  %a2 = schlap_test.read %r {key = "a"} : i32 -> i32
  %x1 = schlap_test.read %a1 {key = "x"} : i32 -> i32
  %x2 = schlap_test.read %a2 {key = "x"} : i32 -> i32
  %b = schlap_test.read %r {key = "b"} : i32 -> i32
  %y = schlap_test.read %r : i32 -> i32
  schlap_test.mutate %a1 : i32
  return
}

// Own every keyed value read from a mutated root but not the root itself.
// CHECK-LABEL: schlap.spec @"InvalidateOwned(root)"
// CHECK: schlap.action @"schlap_test.mutate #3 mutates v0" {
// CHECK: [[V1:%.+]] = schlap.atom "v1"
// CHECK-NEXT: [[V2:%.+]] = schlap.atom "v2"
// CHECK-NEXT: schlap.set_of [[V1]], [[V2]] :
func.func @root() {
  %r = schlap_test.def : i32
  %a = schlap_test.read %r {key = "a"} : i32 -> i32
  %x = schlap_test.read %a {key = "x"} : i32 -> i32
  schlap_test.mutate %r : i32
  return
}

// Own the arguments of a branch by the read-from facts.
// CHECK-LABEL: schlap.spec @"InvalidateOwned(branch)"
// CHECK: schlap.action @"schlap_test.mutate #2 mutates v0" {
// CHECK: [[V1:%.+]] = schlap.atom "v1"
// CHECK-NEXT: schlap.set_of [[V1]] :
func.func @branch() {
  %r = schlap_test.def : i32
  schlap_test.branch %r : i32 {
  ^bb0(%c: i32):
    schlap_test.mutate %r : i32
  }
  return
}
