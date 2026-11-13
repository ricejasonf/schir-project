// RUN: schlap-opt %s | FileCheck %s
// RUN: schlap-opt %s | schlap-opt | FileCheck %s

// CHECK-LABEL: func.func @types(
// CHECK-SAME: %{{.*}}: !schlap.unknown,
// CHECK-SAME: %{{.*}}: !schlap.prop,
// CHECK-SAME: %{{.*}}: !schlap.int,
// CHECK-SAME: %{{.*}}: !schlap.set<!schlap.int>,
// CHECK-SAME: %{{.*}}: !schlap.set<!schlap.set<!schlap.prop>>)
func.func @types(%u: !schlap.unknown, %p: !schlap.prop, %i: !schlap.int,
                 %s: !schlap.set<!schlap.int>,
                 %ss: !schlap.set<!schlap.set<!schlap.prop>>) {
  return
}

// CHECK-LABEL: func.func @formulas(
// CHECK-SAME: [[P:%arg[0-9]+]]: !schlap.prop, [[Q:%arg[0-9]+]]: !schlap.prop,
// CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.int, [[U:%arg[0-9]+]]: !schlap.unknown)
// CHECK-NEXT: %{{.*}} = schlap.and [[P]], [[Q]] : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.or [[P]], [[Q]] : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.not [[P]] : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.implies [[P]], [[Q]] : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.iff [[P]], [[Q]] : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.eq [[A]], [[A]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.ne [[A]], [[U]] : (!schlap.int, !schlap.unknown) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.true : !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.false : !schlap.unknown
// CHECK-NEXT: %{{.*}} = schlap.not [[U]] : (!schlap.unknown) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.and [[U]], [[U]] : !schlap.unknown
func.func @formulas(%p: !schlap.prop, %q: !schlap.prop, %a: !schlap.int,
                    %u: !schlap.unknown) {
  %0 = schlap.and %p, %q : !schlap.prop
  %1 = schlap.or %p, %q : !schlap.prop
  %2 = schlap.not %p : !schlap.prop
  %3 = schlap.implies %p, %q : !schlap.prop
  %4 = schlap.iff %p, %q : !schlap.prop
  %5 = schlap.eq %a, %a : (!schlap.int, !schlap.int) -> !schlap.prop
  %6 = schlap.ne %a, %u : (!schlap.int, !schlap.unknown) -> !schlap.prop
  %7 = schlap.true : !schlap.prop
  %8 = schlap.false : !schlap.unknown
  %9 = schlap.not %u : (!schlap.unknown) -> !schlap.prop
  %10 = schlap.and %u, %u : (!schlap.unknown, !schlap.unknown) -> !schlap.unknown
  return
}

// CHECK-LABEL: func.func @binders(
// CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.set<!schlap.int>,
// CHECK-SAME: [[T:%arg[0-9]+]]: !schlap.unknown)
// CHECK-NEXT: %{{.*}} = schlap.forall ([[X:%.+]]: !schlap.int) in [[S]] : !schlap.set<!schlap.int> -> !schlap.prop {
// CHECK-NEXT: [[ZERO:%.+]] = schlap.int 0 : !schlap.int
// CHECK-NEXT: [[LE:%.+]] = schlap.le [[ZERO]], [[X]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: schlap.yield [[LE]] : !schlap.prop
// CHECK-NEXT: }
// CHECK-NEXT: %{{.*}} = schlap.exists ([[Y:%.+]]: !schlap.unknown) in [[T]] : !schlap.unknown -> !schlap.unknown {
// CHECK-NEXT: [[EQ:%.+]] = schlap.eq [[Y]], [[Y]] : !schlap.unknown
// CHECK-NEXT: schlap.yield [[EQ]] : !schlap.unknown
// CHECK-NEXT: } attributes {note = "kept"}
func.func @binders(%s: !schlap.set<!schlap.int>, %t: !schlap.unknown) {
  %0 = schlap.forall (%x : !schlap.int) in %s
      : !schlap.set<!schlap.int> -> !schlap.prop {
    %1 = schlap.int 0 : !schlap.int
    %2 = schlap.le %1, %x : (!schlap.int, !schlap.int) -> !schlap.prop
    schlap.yield %2 : !schlap.prop
  }
  %3 = schlap.exists (%y : !schlap.unknown) in %t
      : !schlap.unknown -> !schlap.unknown {
    %4 = schlap.eq %y, %y : !schlap.unknown
    schlap.yield %4 : !schlap.unknown
  } attributes {note = "kept"}
  return
}

// CHECK-LABEL: func.func @integers(
// CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.int, [[B:%arg[0-9]+]]: !schlap.int)
// CHECK-NEXT: %{{.*}} = schlap.int -9223372036854775808 : !schlap.int
// CHECK-NEXT: %{{.*}} = schlap.int 42 : !schlap.unknown
// CHECK-NEXT: %{{.*}} = schlap.add [[A]], [[B]] : !schlap.int
// CHECK-NEXT: %{{.*}} = schlap.sub [[A]], [[B]] : !schlap.int
// CHECK-NEXT: %{{.*}} = schlap.mul [[A]], [[B]] : !schlap.int
// CHECK-NEXT: %{{.*}} = schlap.neg [[A]] : !schlap.int
// CHECK-NEXT: %{{.*}} = schlap.lt [[A]], [[B]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.le [[A]], [[B]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.gt [[A]], [[B]] : (!schlap.int, !schlap.int) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.ge [[A]], [[B]] : (!schlap.int, !schlap.int) -> !schlap.prop
func.func @integers(%a: !schlap.int, %b: !schlap.int) {
  %0 = schlap.int -9223372036854775808 : !schlap.int
  %1 = schlap.int 42 : !schlap.unknown
  %2 = schlap.add %a, %b : !schlap.int
  %3 = schlap.sub %a, %b : !schlap.int
  %4 = schlap.mul %a, %b : !schlap.int
  %5 = schlap.neg %a : !schlap.int
  %6 = schlap.lt %a, %b : (!schlap.int, !schlap.int) -> !schlap.prop
  %7 = schlap.le %a, %b : (!schlap.int, !schlap.int) -> !schlap.prop
  %8 = schlap.gt %a, %b : (!schlap.int, !schlap.int) -> !schlap.prop
  %9 = schlap.ge %a, %b : (!schlap.int, !schlap.int) -> !schlap.prop
  return
}

// CHECK-LABEL: func.func @sets(
// CHECK-SAME: [[A:%arg[0-9]+]]: !schlap.int,
// CHECK-SAME: [[S:%arg[0-9]+]]: !schlap.set<!schlap.int>,
// CHECK-SAME: [[T:%arg[0-9]+]]: !schlap.set<!schlap.int>)
// CHECK-NEXT: %{{.*}} = schlap.member [[A]], [[S]] : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.not_member [[A]], [[S]] : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.union [[S]], [[T]] : !schlap.set<!schlap.int>
// CHECK-NEXT: %{{.*}} = schlap.intersect [[S]], [[T]] : !schlap.set<!schlap.int>
// CHECK-NEXT: %{{.*}} = schlap.subset [[S]], [[T]] : (!schlap.set<!schlap.int>, !schlap.set<!schlap.int>) -> !schlap.prop
// CHECK-NEXT: %{{.*}} = schlap.range [[A]], [[A]] : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
// CHECK-NEXT: %{{.*}} = schlap.empty_set : !schlap.set<!schlap.int>
func.func @sets(%a: !schlap.int, %s: !schlap.set<!schlap.int>,
                %t: !schlap.set<!schlap.int>) {
  %0 = schlap.member %a, %s : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
  %1 = schlap.not_member %a, %s : (!schlap.int, !schlap.set<!schlap.int>) -> !schlap.prop
  %2 = schlap.union %s, %t : !schlap.set<!schlap.int>
  %3 = schlap.intersect %s, %t : !schlap.set<!schlap.int>
  %4 = schlap.subset %s, %t : (!schlap.set<!schlap.int>, !schlap.set<!schlap.int>) -> !schlap.prop
  %5 = schlap.range %a, %a : (!schlap.int, !schlap.int) -> !schlap.set<!schlap.int>
  %6 = schlap.empty_set : !schlap.set<!schlap.int>
  return
}
