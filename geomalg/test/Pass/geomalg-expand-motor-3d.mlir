// RUN: geomalg-opt --geomalg-expand="metric=cga" %s | FileCheck %s

// RUN: geomalg-opt --geomalg-to-llvm %s | FileCheck --check-prefix="LLVM" %s

// RUN: geomalg-opt --geomalg-to-spirv %s | FileCheck --check-prefix="SPIRV" %s

// 3D motors lower to SPIRV arrays since vector<8xf32> needs Vector16.

!scalar = !geomalg.blade<0>
!motor3 = !geomalg.multivector<<0>, <3>, <5>, <6>, <17>, <18>, <20>, <23>>

// A 3D motor also corrects for the quadvector part of M M̃.
// CHECK-LABEL: func.func @normalize_3d
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <5>, <6>, <17>, <18>, <20>, <23>>
// CHECK: "geomalg.sqrt"([[A:%[0-9]+]])
// CHECK: "geomalg.inverse"([[A]])
// CHECK: -> !geomalg.blade<23>
// CHECK-NOT: "geomalg.sqrt"
// CHECK: geomalg.return
// LLVM-LABEL: llvm.func @normalize_3d
// LLVM: llvm.intr.sqrt
// LLVM-NOT: llvm.call
// LLVM: llvm.return
// SPIRV-LABEL: spirv.func @normalize_3d
// SPIRV-SAME: (%{{.*}}: !spirv.array<8 x f32>) -> !spirv.array<8 x f32>
// SPIRV: spirv.CompositeConstruct
// SPIRV-SAME: -> !spirv.array<8 x f32>
// SPIRV-NEXT: spirv.ReturnValue
func.func @normalize_3d(%m: !motor3) -> !geomalg.unknown {
  %0 = "geomalg.normalize"(%m) : (!motor3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The motor at t = 3/4 is √D ⁴√D with D = normalize(V₂ Ṽ₁),
// √D = normalize(1 + D), and no trigonometric functions.
// CHECK-LABEL: func.func @interp_three_quarters
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <5>, <6>, <17>, <18>, <20>, <23>>
// CHECK-COUNT-3: "geomalg.sqrt"
// CHECK-NOT: "geomalg.sqrt"
// CHECK-NOT: "geomalg.atan2"
// CHECK-NOT: "geomalg.cos"
// CHECK-NOT: "geomalg.sin"
// CHECK: geomalg.return
// LLVM-LABEL: llvm.func @interp_three_quarters
// LLVM-NOT: llvm.call
// LLVM: llvm.return
// SPIRV-LABEL: spirv.func @interp_three_quarters
// SPIRV-SAME: !spirv.array<8 x f32>, %{{.*}}: !spirv.array<8 x f32>)
// SPIRV-SAME: -> !spirv.array<8 x f32>
// SPIRV-NOT: func.return
// SPIRV: spirv.ReturnValue
func.func @interp_three_quarters(%a: !motor3, %b: !motor3) -> !geomalg.unknown {
  %t = "geomalg.blade"() <{coefficient = 0.75 : f32}> : () -> !scalar
  %0 = "geomalg.interp"(%a, %b, %t) : (!motor3, !motor3, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}
