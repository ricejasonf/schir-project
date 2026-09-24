// RUN: geomalg-opt --geomalg-expand="metric=cga func-name=reflect" %s \
// RUN: | FileCheck %s

// RUN: not geomalg-opt --geomalg-expand="func-name=missing" %s 2>&1 \
// RUN: | FileCheck --check-prefix=MISSING %s

// RUN: not geomalg-opt --geomalg-expand="func-name=external" %s 2>&1 \
// RUN: | FileCheck --check-prefix=EXTERNAL %s

!vec3 = !geomalg.multivector<<1>, <2>, <4>>

// Only the named function is expanded and has its result type updated.
// CHECK-LABEL: func.func @reflect
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <7>>
// CHECK-NOT: "geomalg.vprod"
// CHECK: geomalg.return %{{[0-9]+}} : !geomalg.multivector<<1>, <2>, <4>, <7>>
func.func @reflect(%arg0: !vec3, %arg1: !vec3) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1)
    : (!vec3, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @not_reflect
// CHECK-SAME: -> !geomalg.unknown
// CHECK-NEXT: "geomalg.vprod"
func.func @not_reflect(%arg0: !vec3, %arg1: !vec3) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%arg0, %arg1)
    : (!vec3, !vec3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// MISSING: error: function not found: missing
// EXTERNAL: error: cannot expand external function
func.func private @external(!vec3) -> !vec3
