// RUN: geomalg-opt --geomalg-expand="metric=cga" %s | FileCheck %s

// RUN: geomalg-opt --geomalg-expand="metric=cga" --geomalg-lower %s \
// RUN:| FileCheck --check-prefix="LOWER" %s

// RUN: geomalg-opt --geomalg-expand="metric=cga" \
// RUN:   --geomalg-lower="deterministic=false" %s \
// RUN:| FileCheck --check-prefix="MATH" %s

// RUN: geomalg-opt --geomalg-to-llvm %s | FileCheck --check-prefix="LLVM" %s

// RUN: geomalg-opt --geomalg-to-spirv %s | FileCheck --check-prefix="SPIRV" %s

!scalar = !geomalg.blade<0>
!vec3 = !geomalg.multivector<<1>, <2>, <4>>
!point = !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
!trans = !geomalg.multivector<<17>, <18>, <20>>
!biv2 = !geomalg.multivector<<3>, <17>, <18>>
!biv3 = !geomalg.multivector<<3>, <5>, <6>, <17>, <18>, <20>>
!motor2 = !geomalg.multivector<<0>, <3>, <17>, <18>>
!motor3 = !geomalg.multivector<<0>, <3>, <5>, <6>, <17>, <18>, <20>, <23>>
!umotor2 = !geomalg.unit_multivector<<0>, <3>, <17>, <18>>

// The exponential of a translation bivector is 1 + B.
// CHECK-LABEL: func.func @exp_translation
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <17>, <18>, <20>>
// CHECK-NOT: "geomalg.cos"
// CHECK: [[ONE:%[0-9]+]] = "geomalg.blade"() <{coefficient = 1.0{{.*}}}>
// CHECK-NEXT: "geomalg.sum"([[ONE]],
// CHECK-NEXT: "geomalg.cast"
// CHECK-NEXT: geomalg.return
func.func @exp_translation(%b: !trans) -> !geomalg.unknown {
  %0 = "geomalg.exp"(%b) : (!trans) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A 2D motor is cos φ + sinc φ B with sinc φ = 1 for small angles.
// CHECK-LABEL: func.func @exp_2d
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK: [[PHI:%[0-9]+]] = "geomalg.sqrt"
// CHECK: [[COS:%[0-9]+]] = "geomalg.cos"([[PHI]])
// CHECK: "geomalg.sin"([[PHI]])
// CHECK: [[SINC:%[0-9]+]] = "geomalg.select_lt"
// CHECK: "geomalg.sum"([[COS]],
// CHECK-NOT: "geomalg.gprod"
// CHECK: geomalg.return
// By default sin and cos are polynomials after rounding φ·2/π with 1.5·2²³.
// LOWER-LABEL: func.func @exp_2d
// LOWER: math.sqrt
// LOWER-NOT: math.cos
// LOWER-NOT: math.sin
// LOWER: arith.constant 0.636619746 : f32
// LOWER: arith.constant 0x4B400000 : f32
// LOWER: arith.cmpf olt
// LOWER: arith.select
// LOWER: return
// MATH-LABEL: func.func @exp_2d
// MATH: math.sqrt
// MATH: math.cos
// MATH: math.sin
// MATH: arith.cmpf olt
// MATH: arith.select
// LLVM-LABEL: llvm.func @exp_2d
// LLVM: llvm.intr.sqrt
// LLVM-NOT: llvm.intr.cos
// LLVM-NOT: llvm.intr.sin
// LLVM-NOT: llvm.call
// LLVM: llvm.select
// LLVM: llvm.return
// SPIRV-LABEL: spirv.func @exp_2d
// SPIRV: spirv.GL.Sqrt
// SPIRV: spirv.GL.Cos
// SPIRV: spirv.GL.Sin
// SPIRV: spirv.FOrdLessThan
// SPIRV: spirv.Select
func.func @exp_2d(%b: !biv2) -> !geomalg.unknown {
  %0 = "geomalg.exp"(%b) : (!biv2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @exp_3d
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <5>, <6>, <17>, <18>, <20>, <23>>
// CHECK-NOT: "geomalg.gprod"
// CHECK-NOT: "geomalg.oprod"
// CHECK: geomalg.return
func.func @exp_3d(%b: !biv3) -> !geomalg.unknown {
  %0 = "geomalg.exp"(%b) : (!biv3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @log_3d
// CHECK-SAME: -> !geomalg.multivector<<3>, <5>, <6>, <17>, <18>, <20>>
// CHECK: "geomalg.atan2"
// CHECK: geomalg.return
// LOWER-LABEL: func.func @log_3d
// LOWER-NOT: math.atan2
// LOWER: math.copysign
// LOWER: return
// MATH-LABEL: func.func @log_3d
// MATH: math.atan2
// LLVM-LABEL: llvm.func @log_3d
// LLVM-NOT: llvm.intr.atan2
// LLVM-NOT: llvm.call
// LLVM: llvm.intr.copysign
// LLVM: llvm.return
func.func @log_3d(%m: !motor3) -> !geomalg.unknown {
  %0 = "geomalg.log"(%m) : (!motor3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @log_2d
// CHECK-SAME: -> !geomalg.multivector<<3>, <17>, <18>>
// CHECK: "geomalg.atan2"
// CHECK: geomalg.return
// SPIRV-LABEL: spirv.func @log_2d
// SPIRV: spirv.GL.Atan2
func.func @log_2d(%m: !motor2) -> !geomalg.unknown {
  %0 = "geomalg.log"(%m) : (!motor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// Without a scalar part the angle is π/2.
// CHECK-LABEL: func.func @log_half_turn
// CHECK-SAME: -> !geomalg.blade<3>
// CHECK: [[R:%[0-9]+]] = "geomalg.sqrt"
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK: "geomalg.atan2"([[R]], [[ZERO]])
// CHECK-NOT: "geomalg.select_lt"
// CHECK: geomalg.return
func.func @log_half_turn(%m: !geomalg.blade<3>) -> !geomalg.unknown {
  %0 = "geomalg.log"(%m) : (!geomalg.blade<3>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The log of s + Bₜ is Bₜ / s.
// CHECK-LABEL: func.func @log_translation
// CHECK-SAME: -> !geomalg.multivector<<17>, <18>, <20>>
// CHECK-NOT: "geomalg.atan2"
// CHECK: "geomalg.inverse"
// CHECK: geomalg.return
func.func @log_translation(%m: !geomalg.multivector<<0>, <17>, <18>, <20>>) -> !geomalg.unknown {
  %0 = "geomalg.log"(%m) : (!geomalg.multivector<<0>, <17>, <18>, <20>>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// CHECK-LABEL: func.func @grade
// CHECK-SAME: -> !geomalg.multivector<<3>, <5>, <6>, <17>, <18>, <20>>
// CHECK: [[E:%[0-9]+]]:8 = "geomalg.expand"
// CHECK-NEXT: "geomalg.sum"([[E]]#1, [[E]]#2, [[E]]#3, [[E]]#4, [[E]]#5, [[E]]#6)
// CHECK-NEXT: geomalg.return
func.func @grade(%m: !motor3) -> !geomalg.unknown {
  %0 = "geomalg.grade"(%m) <{grade = 2 : i32}> : (!motor3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A motor maps a point to a 1-vector.
// CHECK-LABEL: func.func @vprod_motor
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK-NOT: "geomalg.vprod"
// CHECK: geomalg.return
func.func @vprod_motor(%x: !point, %m: !motor3) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %m) : (!point, !motor3) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// An even versor commutes with an orthogonal blade.
// CHECK-LABEL: func.func @vprod_rotor_orthogonal
// CHECK-SAME: ([[X:%arg[0-9]+]]: !geomalg.blade<4>
// CHECK-NEXT: geomalg.return [[X]] : !geomalg.blade<4>
func.func @vprod_rotor_orthogonal(%x: !geomalg.blade<4>, %m: !geomalg.multivector<<0>, <3>>) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %m) : (!geomalg.blade<4>, !geomalg.multivector<<0>, <3>>) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The inverse of a versor is Ṽ / ⟨V Ṽ⟩₀.
// CHECK-LABEL: func.func @inverse_motor
// CHECK-SAME: -> !geomalg.multivector<<0>, <3>, <17>, <18>>
// CHECK: [[INV:%[0-9]+]] = "geomalg.inverse"
// CHECK-SAME: (!geomalg.blade<0>) -> !geomalg.blade<0>
// CHECK: "geomalg.cmul"({{.*}}, [[INV]])
// CHECK: geomalg.return
func.func @inverse_motor(%m: !motor2) -> !geomalg.unknown {
  %0 = "geomalg.inverse"(%m) : (!motor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A unit motor is applied as V X Ṽ without dividing by ⟨V Ṽ⟩₀.
// CHECK-LABEL: func.func @vprod_unit_motor
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK-NOT: "geomalg.inverse"
// CHECK: geomalg.return
// LLVM-LABEL: llvm.func @vprod_unit_motor
// LLVM-NOT: llvm.fdiv
// LLVM: llvm.return
func.func @vprod_unit_motor(%x: !point, %m: !umotor2) -> !geomalg.unknown {
  %0 = "geomalg.vprod"(%x, %m) : (!point, !umotor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The motor exp(B) is a unit motor so only sinc φ divides.
// CHECK-LABEL: func.func @vprod_exp
// CHECK-SAME: -> !geomalg.multivector<<1>, <2>, <4>, <8>, <16>>
// CHECK: [[PHI:%[0-9]+]] = "geomalg.sqrt"
// CHECK: "geomalg.inverse"([[PHI]])
// CHECK-NOT: "geomalg.inverse"
// CHECK: geomalg.return
func.func @vprod_exp(%x: !point, %b: !biv2) -> !geomalg.unknown {
  %m = "geomalg.exp"(%b) : (!biv2) -> !geomalg.unknown
  %0 = "geomalg.vprod"(%x, %m) : (!point, !geomalg.unknown) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The product of unit motors is a unit motor.
// CHECK-LABEL: func.func @gprod_unit_motors
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK-NOT: "geomalg.inverse"
// CHECK: geomalg.return
func.func @gprod_unit_motors(%a: !umotor2, %b: !umotor2) -> !geomalg.unknown {
  %0 = "geomalg.gprod"(%a, %b) : (!umotor2, !umotor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The inverse of a unit motor is its reverse.
// CHECK-LABEL: func.func @inverse_unit_motor
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK-NOT: "geomalg.inverse"
// CHECK: [[E:%[0-9]+]]:4 = "geomalg.expand"
// CHECK: [[N1:%[0-9]+]] = "geomalg.negate"([[E]]#1)
// CHECK: [[N2:%[0-9]+]] = "geomalg.negate"([[E]]#2)
// CHECK: [[N3:%[0-9]+]] = "geomalg.negate"([[E]]#3)
// CHECK: "geomalg.sum"([[E]]#0, [[N1]], [[N2]], [[N3]])
// CHECK: geomalg.return
func.func @inverse_unit_motor(%m: !umotor2) -> !geomalg.unknown {
  %0 = "geomalg.inverse"(%m) : (!umotor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// normalize(M) = σ M for a unit motor M.
// CHECK-LABEL: func.func @normalize_unit_motor
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK-NOT: "geomalg.sqrt"
// CHECK-NOT: "geomalg.inverse"
// CHECK: "geomalg.select_lt"
// CHECK: geomalg.return
func.func @normalize_unit_motor(%m: !umotor2) -> !geomalg.unknown {
  %0 = "geomalg.normalize"(%m) : (!umotor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// normalize(M) = σ M / √⟨M M̃⟩₀ in 2D with σ the sign of the scalar part.
// CHECK-LABEL: func.func @normalize_2d
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK: [[E:%[0-9]+]]:4 = "geomalg.expand"
// CHECK: [[SQRT:%[0-9]+]] = "geomalg.sqrt"
// CHECK: [[INV:%[0-9]+]] = "geomalg.inverse"([[SQRT]])
// CHECK: [[NEG:%[0-9]+]] = "geomalg.negate"([[INV]])
// CHECK: [[ZERO:%[0-9]+]] = "geomalg.blade"() <{coefficient = 0.0{{.*}}}>
// CHECK: "geomalg.select_lt"([[E]]#0, [[ZERO]], [[NEG]], [[INV]])
// CHECK-NOT: "geomalg.sqrt"
// CHECK: geomalg.return
func.func @normalize_2d(%m: !motor2) -> !geomalg.unknown {
  %0 = "geomalg.normalize"(%m) : (!motor2) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// Interpolation with a run-time t is exp(t log(V₂ Ṽ₁)).
// CHECK-LABEL: func.func @interp_exact
// CHECK: "geomalg.atan2"
// CHECK: "geomalg.cos"
// CHECK: "geomalg.sin"
// CHECK: geomalg.return
func.func @interp_exact(%a: !motor2, %b: !motor2, %t: !scalar) -> !geomalg.unknown {
  %0 = "geomalg.interp"(%a, %b, %t) : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The motor at t = 0 is the identity.
// CHECK-LABEL: func.func @interp_zero
// CHECK-SAME: -> !geomalg.blade<0>
// CHECK-NEXT: [[ONE:%[0-9]+]] = "geomalg.blade"() <{coefficient = 1.0{{.*}}}>
// CHECK-NEXT: geomalg.return [[ONE]]
func.func @interp_zero(%a: !motor2, %b: !motor2) -> !geomalg.unknown {
  %t = "geomalg.blade"() <{coefficient = 0.0 : f32}> : () -> !scalar
  %0 = "geomalg.interp"(%a, %b, %t) : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// The motor at t = 1 is normalize(V₂ Ṽ₁).
// CHECK-LABEL: func.func @interp_one
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK: "geomalg.sqrt"
// CHECK-NOT: "geomalg.sqrt"
// CHECK-NOT: "geomalg.atan2"
// CHECK: geomalg.return
func.func @interp_one(%a: !motor2, %b: !motor2) -> !geomalg.unknown {
  %t = "geomalg.blade"() <{coefficient = 1.0 : f32}> : () -> !scalar
  %0 = "geomalg.interp"(%a, %b, %t) : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// A constant t that is not k / 2ⁿ with n ≤ 4 uses exp and log.
// CHECK-LABEL: func.func @interp_tenth
// CHECK: "geomalg.atan2"
// CHECK: geomalg.return
func.func @interp_tenth(%a: !motor2, %b: !motor2) -> !geomalg.unknown {
  %t = "geomalg.blade"() <{coefficient = 0.1 : f32}> : () -> !scalar
  %0 = "geomalg.interp"(%a, %b, %t) : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// nlerp is normalize((1 - t) + t D) with D = normalize(V₂ Ṽ₁).
// CHECK-LABEL: func.func @nlerp
// CHECK-SAME: -> !geomalg.unit_multivector<<0>, <3>, <17>, <18>>
// CHECK-COUNT-2: "geomalg.sqrt"
// CHECK-NOT: "geomalg.sqrt"
// CHECK-NOT: "geomalg.atan2"
// CHECK-NOT: "geomalg.cos"
// CHECK: geomalg.return
// SPIRV-LABEL: spirv.func @nlerp
// SPIRV-NOT: spirv.GL.Atan2
// SPIRV-NOT: spirv.GL.Cos
// SPIRV: spirv.GL.Sqrt
// SPIRV: spirv.GL.Sqrt
// SPIRV: spirv.ReturnValue
func.func @nlerp(%a: !motor2, %b: !motor2, %t: !scalar) -> !geomalg.unknown {
  %0 = "geomalg.interp"(%a, %b, %t) <{method = #geomalg.interp_method<nlerp>}> : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}

// nlerp at t = ½ is √D like interp.
// CHECK-LABEL: func.func @nlerp_half
// CHECK-COUNT-2: "geomalg.sqrt"
// CHECK-NOT: "geomalg.sqrt"
// CHECK: geomalg.return
func.func @nlerp_half(%a: !motor2, %b: !motor2) -> !geomalg.unknown {
  %t = "geomalg.blade"() <{coefficient = 0.5 : f32}> : () -> !scalar
  %0 = "geomalg.interp"(%a, %b, %t) <{method = #geomalg.interp_method<nlerp>}> : (!motor2, !motor2, !scalar) -> !geomalg.unknown
  geomalg.return %0 : !geomalg.unknown
}
