// RUN: geomalg-opt --geomalg-lower-to-spirv %s | FileCheck %s

// Vectors with no SPIRV vector type for the target are lowered to arrays.
module attributes {spirv.target_env = #spirv.target_env<
    #spirv.vce<v1.0, [Shader, Vector16], []>, #spirv.resource_limits<>>} {
// CHECK-LABEL: spirv.func @vector16
// CHECK-SAME: (%{{.*}}: vector<8xf32>, %{{.*}}: !spirv.array<6 x f32>)
// CHECK-SAME: -> !spirv.array<6 x f32>
// CHECK: spirv.CompositeExtract %{{.*}}[0 : i32] : vector<8xf32>
// CHECK: spirv.CompositeConstruct
// CHECK-SAME: -> !spirv.array<6 x f32>
// CHECK-NEXT: spirv.ReturnValue
func.func @vector16(%a: vector<8xf32>, %b: vector<6xf32>) -> vector<6xf32> {
  %e:8 = vector.to_elements %a : vector<8xf32>
  %r = vector.from_elements %e#0, %e#1, %e#2, %e#3, %e#4, %e#5 : vector<6xf32>
  return %r : vector<6xf32>
}
}
