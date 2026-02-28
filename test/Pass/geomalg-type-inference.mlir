// RUN: geomalg-opt \
// RUN:   --pass-pipeline="builtin.module(func.func(geomalg-type-inference))" %s \
// RUN:   | FileCheck %s

module {
// CHECK: func.func @my_func1
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <3>, <2147483651>>
// CHECK-NEXT: "geomalg.sum"
// CHECK-SAME:  -> !geomalg.multivector<<3>, <2147483651>>
// CHECK-NEXT: "geomalg.sum"
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <3>, <2147483651>>
  func.func @my_func1(%arg0: !geomalg.blade<1>, %arg1: !geomalg.blade<2>,
                      %arg2: !geomalg.blade<3>, %arg3: !geomalg.blade<2147483651>)
                      -> !geomalg.unknown {
    %0 = "geomalg.sum"(%arg2, %arg3)
      : (!geomalg.blade<3>, !geomalg.blade<2147483651>) -> !geomalg.unknown
    %1 = "geomalg.sum"(%arg0, %arg1, %0)
      : (!geomalg.blade<1>, !geomalg.blade<2>, !geomalg.unknown) -> !geomalg.unknown
    return %1 : !geomalg.unknown
  }

// CHECK: func.func @my_func2
// CHECK-SAME: -> !geomalg.blade<7>
// CHECK-NEXT: "geomalg.sum"
// CHECK-SAME:  -> !geomalg.blade<7>
// CHECK-NEXT:  return
// CHECK-SAME: : !geomalg.blade<7>
  func.func @my_func2(%arg0: !geomalg.blade<7>,
                      %arg1: !geomalg.blade<7>) -> !geomalg.unknown {
    %0 = "geomalg.sum"(%arg0, %arg1)
      : (!geomalg.blade<7>, !geomalg.blade<7>) -> !geomalg.unknown
    return %0 : !geomalg.unknown
  }

// CHECK: func.func @my_func3
// CHECK-SAME: -> !geomalg.blade<15>
// CHECK-NEXT: "geomalg.sum"
// CHECK-SAME:  -> !geomalg.blade<15>
// CHECK-NEXT:  return
// CHECK-SAME: : !geomalg.blade<15>
  func.func @my_func3(%arg0: !geomalg.blade<15>, %arg1: !geomalg.blade<15>) -> !geomalg.unknown {
    %0 = "geomalg.sum"(%arg0, %arg1)
      : (!geomalg.blade<15>, !geomalg.blade<15>) -> !geomalg.unknown
    return %0 : !geomalg.unknown
  }

// CHECK: func.func @my_func4
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <3>>
// CHECK-NEXT: "geomalg.sum"
// CHECK-SAME:  -> !geomalg.multivector<<1>, <2>, <3>>
// CHECK-NEXT:  return
// CHECK-SAME: : !geomalg.multivector<<1>, <2>, <3>>
  func.func @my_func4(%arg0: !geomalg.multivector<<1>, <2>, <3>>) -> !geomalg.unknown {
    %0 = "geomalg.sum"(%arg0)
      : (!geomalg.multivector<<1>, <2>, <3>>) -> !geomalg.unknown
    return %0 : !geomalg.unknown
  }
}
