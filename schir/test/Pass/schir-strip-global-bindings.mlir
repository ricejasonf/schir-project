// RUN: schir-opt \
// RUN:   --pass-pipeline="builtin.module(builtin.module(schir-strip-global-bindings))" %s \
// RUN:   | FileCheck %s
module {
  module @_SCHIR {
    "schir.global"() <{sym_name = "_SCHIRL5SschirL8SbuiltinsVpl", type = !schir.procedure}> ({
    }) : () -> ()
    "schir.load_module"() <{name = @_SCHIRL5SschirL8Sbuiltins}> : () -> ()
// CHECK-LABEL: "schir.global"() <{sym_name = "_SCHIRV3Sgetmi4Sfive", type = !schir.procedure}>
    "schir.global"() <{sym_name = "_SCHIRV3Sgetmi4Sfive", type = !schir.binding}> ({
      %0 = "schir.lambda"() <{name = "_SCHIRA0"}> : () -> !schir.procedure
// CHECK-NOT: "schir.binding"
      %1 = "schir.binding"(%0) : (!schir.procedure) -> !schir.binding
// CHECK: "schir.init_global"({{.*}}) <{name = @_SCHIRV3Sgetmi4Sfive}> : (!schir.procedure) -> ()
      "schir.init_global"(%1) <{name = @_SCHIRV3Sgetmi4Sfive}> : (!schir.binding) -> ()
      %2 = "schir.undefined"() : () -> !schir.undefined
      "schir.cont"(%2) : (!schir.undefined) -> ()
    }) : () -> ()
    func.func @_SCHIRA0(%arg0: !schir.context) {
      %0 = "schir.literal"() <{input = #schir<"5">}> : () -> !schir.value
      "schir.cont"(%0) : (!schir.value) -> ()
    }
// CHECK-LABEL: "schir.global"() <{sym_name = "_SCHIRV3Sgetmi4Sfivemi3Smut", type = !schir.binding}>
    "schir.global"() <{sym_name = "_SCHIRV3Sgetmi4Sfivemi3Smut", type = !schir.binding}> ({
      %0 = "schir.lambda"() <{name = "_SCHIRA1"}> : () -> !schir.procedure
// CHECK: [[RES:%[0-9]]] = "schir.binding"
      %1 = "schir.binding"(%0) : (!schir.procedure) -> !schir.binding
// CHECK: "schir.init_global"([[RES]]) <{name = @_SCHIRV3Sgetmi4Sfivemi3Smut}> : (!schir.binding) -> ()
      "schir.init_global"(%1) <{name = @_SCHIRV3Sgetmi4Sfivemi3Smut}> : (!schir.binding) -> ()
      %2 = "schir.undefined"() : () -> !schir.undefined
      "schir.cont"(%2) : (!schir.undefined) -> ()
    }) : () -> ()
    func.func @_SCHIRA1(%arg0: !schir.context) {
      %0 = "schir.literal"() <{input = #schir<"#f">}> : () -> !schir.value
      "schir.cont"(%0) : (!schir.value) -> ()
    }
// CHECK-LABEL: "schir.global"() <{sym_name = "_SCHIRV3Saddmi5Sfives", type = !schir.procedure}>
    "schir.global"() <{sym_name = "_SCHIRV3Saddmi5Sfives", type = !schir.binding}> ({
      %0 = "schir.lambda"() <{name = "_SCHIRA2"}> : () -> !schir.procedure
      %1 = "schir.binding"(%0) : (!schir.procedure) -> !schir.binding
// CHECK: "schir.init_global"({{.*}}) <{name = @_SCHIRV3Saddmi5Sfives}> : (!schir.procedure) -> ()
      "schir.init_global"(%1) <{name = @_SCHIRV3Saddmi5Sfives}> : (!schir.binding) -> ()
      %2 = "schir.undefined"() : () -> !schir.undefined
      "schir.cont"(%2) : (!schir.undefined) -> ()
    }) : () -> ()
// CHECK-LABEL: @_SCHIRA2
    func.func @_SCHIRA2(%arg0: !schir.context) {
// CHECK: [[RES1:%[0-9]]] = "schir.load_global"() <{name = @_SCHIRV3Sgetmi4Sfive}> : () -> !schir.procedure
// CHECK-NOT: "schir.unbox"
// CHECK: [[RES2:%[0-9]]] = "schir.load_global"() <{name = @_SCHIRV3Sgetmi4Sfivemi3Smut}> : () -> !schir.binding
// CHECK: "schir.set"([[RES2]], [[RES1]]) : (!schir.binding, !schir.procedure) -> !schir.value
// CHECK-NOT: "schir.match_type"([[RES1]])
      %0 = "schir.load_global"() <{name = @_SCHIRV3Sgetmi4Sfive}> : () -> !schir.binding
      %1 = "schir.unbox"(%0) : (!schir.binding) -> !schir.value
      %2 = "schir.load_global"() <{name = @_SCHIRV3Sgetmi4Sfivemi3Smut}> : () -> !schir.binding
      %3 = "schir.set"(%2, %1) : (!schir.binding, !schir.value) -> !schir.value
      %4 = "schir.load_global"() <{name = @_SCHIRL5SschirL8SbuiltinsVpl}> : () -> !schir.procedure
      %5 = "schir.match_type"(%4) : (!schir.procedure) -> !schir.procedure
      %6 = "schir.match_type"(%0) : (!schir.binding) -> !schir.procedure
      "schir.push_cont"(%2, %5) <{name = "_SCHIRA3"}> : (!schir.binding, !schir.procedure) -> ()
      "schir.apply"(%6) : (!schir.procedure) -> ()
    }
// CHECK-LABEL @_SCHIRA3
    func.func @_SCHIRA3(%arg0: !schir.context, %arg1: !schir.value_refs) {
      %0 = "schir.load_ref"(%arg0) <{index = 1 : ui32}> : (!schir.context) -> !schir.procedure
      %1 = "schir.load_ref"(%arg0) <{index = 0 : ui32}> : (!schir.context) -> !schir.binding
      %2 = "schir.load_ref"(%arg1) <{index = 0 : ui32}> : (!schir.value_refs) -> !schir.value
      %3 = "schir.match_type"(%1) : (!schir.binding) -> !schir.procedure
      "schir.push_cont"(%0, %2) <{name = "_SCHIRA4"}> : (!schir.procedure, !schir.value) -> ()
      "schir.apply"(%3) : (!schir.procedure) -> ()
    }
// CHECK-LABEL @_SCHIRA4
    func.func @_SCHIRA4(%arg0: !schir.context, %arg1: !schir.value_refs) {
      %0 = "schir.load_ref"(%arg0) <{index = 1 : ui32}> : (!schir.context) -> !schir.value
      %1 = "schir.load_ref"(%arg0) <{index = 0 : ui32}> : (!schir.context) -> !schir.procedure
      %2 = "schir.load_ref"(%arg1) <{index = 0 : ui32}> : (!schir.value_refs) -> !schir.value
      "schir.apply"(%1, %0, %2) : (!schir.procedure, !schir.value, !schir.value) -> ()
    }
    "schir.command"() ({
// CHECK-LABEL: "schir.command"
// CHECK: %0 = "schir.load_global"() <{name = @_SCHIRV3Saddmi5Sfives}> : () -> !schir.procedure
// CHECK-NOT: "schir.match_type"
      %0 = "schir.load_global"() <{name = @_SCHIRV3Saddmi5Sfives}> : () -> !schir.binding
      %1 = "schir.match_type"(%0) : (!schir.binding) -> !schir.procedure
      "schir.apply"(%1) : (!schir.procedure) -> ()
    }) : () -> ()
  }
}
