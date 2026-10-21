// RUN: geomalg-opt --geomalg-simplify %s | FileCheck %s

!scalar = !geomalg.blade<0>
!e1 = !geomalg.blade<1>

// Like terms combine even if they are not adjacent.
// CHECK-LABEL: func.func @combine_interleaved
// CHECK-SAME: ([[A:%arg[0-9]+]]: !geomalg.blade<0>, [[B:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK-DAG: [[TWO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 2.0{{.*}}}>
// CHECK-DAG: [[MA:%[0-9]+]] = "geomalg.cmul"([[A]], [[TWO]])
// CHECK-DAG: [[MB:%[0-9]+]] = "geomalg.cmul"([[B]], [[TWO]])
// CHECK: "geomalg.sum"([[MA]], [[MB]])
func.func @combine_interleaved(%a: !scalar, %b: !scalar) -> !scalar {
  %0 = "geomalg.sum"(%a, %b, %a, %b) : (!scalar, !scalar, !scalar, !scalar) -> !scalar
  geomalg.return %0 : !scalar
}

// An odd number of like terms keeps its count.
// CHECK-LABEL: func.func @combine_three
// CHECK-SAME: ([[A:%arg[0-9]+]]: !geomalg.blade<0>)
// CHECK: [[THREE:%[0-9]+]] = "geomalg.blade"() <{coefficient = 3.0{{.*}}}>
// CHECK: [[M:%[0-9]+]] = "geomalg.cmul"([[A]], [[THREE]])
// CHECK-NOT: "geomalg.sum"
// CHECK: geomalg.return [[M]]
func.func @combine_three(%a: !scalar) -> !scalar {
  %0 = "geomalg.sum"(%a, %a, %a) : (!scalar, !scalar, !scalar) -> !scalar
  geomalg.return %0 : !scalar
}

// Constant multiples and negations of the same product combine.
// CHECK-LABEL: func.func @combine_products
// CHECK-SAME: ([[A:%arg[0-9]+]]: !geomalg.blade<0>, [[B:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[C:%[0-9]+]] = "geomalg.blade"() <{coefficient = 1.5{{.*}}}>
// CHECK: [[M:%[0-9]+]] = "geomalg.cmul"([[A]], [[B]], [[C]])
// CHECK-NOT: "geomalg.sum"
// CHECK: geomalg.return [[M]]
func.func @combine_products(%a: !scalar, %b: !e1) -> !e1 {
  %c = "geomalg.blade"() <{coefficient = 2.500000e+00 : f32}> : () -> !scalar
  %0 = "geomalg.cmul"(%a, %b) : (!scalar, !e1) -> !e1
  %1 = "geomalg.cmul"(%b, %a, %c) : (!e1, !scalar, !scalar) -> !e1
  %2 = "geomalg.negate"(%0) : (!e1) -> !e1
  %3 = "geomalg.sum"(%0, %1, %2, %2) : (!e1, !e1, !e1, !e1) -> !e1
  geomalg.return %3 : !e1
}

// Constant blades of the same type combine into one constant.
// CHECK-LABEL: func.func @combine_constants
// CHECK-SAME: ([[A:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: [[C:%[0-9]+]] = "geomalg.blade"() <{coefficient = 5.0{{.*}}}> : () -> !geomalg.blade<1>
// CHECK: "geomalg.sum"([[C]], [[A]])
func.func @combine_constants(%a: !e1) -> !e1 {
  %0 = "geomalg.blade"() <{coefficient = 2.000000e+00 : f32}> : () -> !e1
  %1 = "geomalg.blade"() <{coefficient = 3.000000e+00 : f32}> : () -> !e1
  %2 = "geomalg.sum"(%0, %a, %1) : (!e1, !e1, !e1) -> !e1
  geomalg.return %2 : !e1
}

// Terms that cancel are removed.
// CHECK-LABEL: func.func @cancel
// CHECK-SAME: ([[A:%arg[0-9]+]]: !geomalg.blade<1>, [[B:%arg[0-9]+]]: !geomalg.blade<1>)
// CHECK: geomalg.return [[B]]
func.func @cancel(%a: !e1, %b: !e1) -> !e1 {
  %0 = "geomalg.negate"(%a) : (!e1) -> !e1
  %1 = "geomalg.sum"(%a, %b, %0) : (!e1, !e1, !e1) -> !e1
  geomalg.return %1 : !e1
}
