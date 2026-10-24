// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   -S -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv
// RUN: spirv-dis %t.dir/shaders.spv | FileCheck --check-prefix=SPIRV %s
// RUN: clang++ %t.ll -o %t
// RUN: %t

// The binary is a private constant returned via a C adapter.
// LLVM: @"::foo::shaders.spv" = private constant [{{[0-9]+}} x i32] [i32 119734787,
// LLVM-SAME: align 4
// LLVM-LABEL: define void @nbdl_c_3foo7shaders(ptr %0)

// SPIRV-DAG: OpEntryPoint Fragment %fs_main "fs_main"
// SPIRV-DAG: OpEntryPoint Vertex %vs_main "vs_main"
// SPIRV-DAG: OpEntryPoint GLCompute %cs_main "cs_main"
// SPIRV-DAG: OpExecutionMode %cs_main LocalSize 64 2 1

#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <cstdint>
#include <string_view>

namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) create-op result))

(export-c scale)
(export-shader scale fs_main vs_main cs_main)
(export-spirv-binary shaders)

(define !vec4 (type "vector<4xf32>"))
(define !uvec3 (type "vector<3xi32>"))

(define Scale (shader-global 'uniform-buffer !vec4))
(define Offset (shader-global 'uniform-buffer !vec4))
(define Position (shader-global 'builtin "Position" !vec4))
(define InvocationId (shader-global 'builtin "GlobalInvocationId" !uvec3))

; // A function exported to C and a shader is in both binaries.
(define-fn scale ((V : !vec4) (S : !vec4)) -> (!vec4)
  (return (result (create-op "arith.mulf"
                             (loc: 0)
                             (operands: V S)
                             (attributes:)
                             (result-types: !vec4)))))

(define-shader-entrypoint fs_main "Fragment" ((Color : !vec4)) -> (!vec4)
  (return Color))

(define-shader-entrypoint vs_main "Vertex"
    ((P : !vec4) (S : Scale) (O : Offset)) -> (Position)
  (return (visit scale P S)))

(define-shader-entrypoint cs_main ("Compute" 64 2 1)
    ((I : InvocationId)) -> ()
  (return))

(write-spirv-binary "shaders.spv")
(finalize-module)
} // schir_scheme
} // namespace foo
} // namespace

// Describe each entry point in order of definition.
static_assert(foo::shaders_entries.size() == 3);
static_assert(std::string_view(foo::shaders_entries[0].name) == "fs_main");
static_assert(foo::shaders_entries[0].stage == nbdl::shader_stage::fragment);
static_assert(foo::shaders_entries[0].num_uniform_buffers == 0);
static_assert(std::string_view(foo::shaders_entries[1].name) == "vs_main");
static_assert(foo::shaders_entries[1].stage == nbdl::shader_stage::vertex);
static_assert(foo::shaders_entries[1].num_uniform_buffers == 2);
static_assert(foo::shaders_entries[1].num_samplers == 0);
static_assert(foo::shaders_entries[1].num_storage_textures == 0);
static_assert(foo::shaders_entries[1].num_storage_buffers == 0);
static_assert(std::string_view(foo::shaders_entries[2].name) == "cs_main");
static_assert(foo::shaders_entries[2].stage == nbdl::shader_stage::compute);
static_assert(foo::shaders_entries[2].threadcount[0] == 64);
static_assert(foo::shaders_entries[2].threadcount[1] == 2);
static_assert(foo::shaders_entries[2].threadcount[2] == 1);

int main() {
  // Receive the SPIRV words of the binary.
  nbdl::memref<std::int32_t, 1> code = foo::shaders();
  SCHIR_ASSERT(code.size > 5);
  SCHIR_ASSERT(code.stride == 1);
  SCHIR_ASSERT(static_cast<std::uint32_t>(code.aligned[code.offset])
               == 0x07230203u);

  // The function exported to C still works.
  nbdl::vec_f32<4> v = foo::scale(nbdl::vec_f32<4>{1, 2, 3, 4},
                                  nbdl::vec_f32<4>{2, 2, 2, 2});
  SCHIR_ASSERT(v[3] == 8);
}
