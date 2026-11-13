// RUN: schlap-opt %s -canonicalize | FileCheck %s

// Fold constant integers and formulas.
// CHECK-LABEL: func.func @ints(
// CHECK-NEXT: [[SEVEN:%.+]] = schlap.int 7 : !schlap.int
// CHECK-NEXT: [[TRUE:%.+]] = schlap.true : !schlap.prop
// CHECK-NEXT: return [[SEVEN]], [[TRUE]], %arg0 : !schlap.int, !schlap.prop, !schlap.prop
func.func @ints(%p: !schlap.prop) -> (!schlap.int, !schlap.prop, !schlap.prop) {
  %0 = schlap.int 3 : !schlap.int
  %1 = schlap.int 4 : !schlap.int
  %2 = schlap.add %0, %1 : !schlap.int
  %3 = schlap.lt %0, %1 : (!schlap.int, !schlap.int) -> !schlap.prop
  %4 = schlap.and %3, %p : !schlap.prop
  return %2, %3, %4 : !schlap.int, !schlap.prop, !schlap.prop
}

// Do not fold an addition that overflows.
// CHECK-LABEL: func.func @overflow(
// CHECK: schlap.add
func.func @overflow() -> !schlap.int {
  %0 = schlap.int 9223372036854775807 : !schlap.int
  %1 = schlap.add %0, %0 : !schlap.int
  return %1 : !schlap.int
}

// Fold set operations whose operands materialize as constants and keep
// those whose operands are nonempty sets.
// CHECK-LABEL: func.func @sets(
// CHECK-NEXT: [[FALSE:%.+]] = schlap.false : !schlap.prop
// CHECK-NEXT: [[EMPTY:%.+]] = schlap.empty_set : !schlap.set<!schlap.int>
// CHECK: [[SUB:%.+]] = schlap.subset
// CHECK-NEXT: [[INT:%.+]] = schlap.intersect
// CHECK-NEXT: return [[SUB]], [[FALSE]], [[INT]]
func.func @sets() -> (!schlap.prop, !schlap.prop, !schlap.set<!schlap.int>) {
  %0 = schlap.int 1 : !schlap.int
  %1 = schlap.int 3 : !schlap.int
  %2 = schlap.range %0, %1 : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
  %3 = schlap.set_of %1, %0 : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
  %4 = schlap.subset %3, %2 : (!schlap.set<!schlap.int>, !schlap.set<!schlap.int>) -> !schlap.prop
  %5 = schlap.empty_set : !schlap.set<!schlap.int>
  %6 = schlap.member %0, %5 : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
  %7 = schlap.intersect %3, %5 : !schlap.set<!schlap.int>
  return %4, %6, %7 : !schlap.prop, !schlap.prop, !schlap.set<!schlap.int>
}
