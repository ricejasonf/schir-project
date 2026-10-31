// REQUIRES: sdl3-gpu
// RUN: rm -rf %t.dir && mkdir %t.dir && cd %t.dir && \
// RUN:   clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   -S -emit-llvm %s -o %t.ll
// RUN: spirv-val --target-env vulkan1.0 %t.dir/shaders.spv
// RUN: clang++ %t.ll -lSDL3 -o %t
// RUN: %t

#include <nbdl/spec.hpp>
#include <nbdl/spec/sdl3.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstdio>

// Render a triangle with a vertex and fragment entry point pair on a GPU via
// the SDL3 adapter and check its pixels.
namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op load-dialect result))

(load-dialect "vector")
(export-shader vs_triangle fs_color)
(export-spirv-binary shaders)

(define f32 (type "f32"))
(define !vec4 (type "vector<4xf32>"))

(define VertexIndex (shader-global 'builtin "VertexIndex" (type "i32")))
(define Position (shader-global 'builtin "Position" !vec4))

(define (constant Value Type)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr Value Type)))
                     (result-types: Type))))

(define (binary-op Name A B)
  (result (create-op Name
                     (loc: 0)
                     (operands: A B)
                     (attributes:)
                     (result-types: f32))))

; // Place the vertices at (-0.5, -0.5), (0, 0.5), and (0.5, -0.5) and give
; // each one opaque red.
(define-shader-entrypoint vs_triangle "Vertex"
    ((I : VertexIndex)) -> (Position !vec4)
  (define Half (constant "0.5" f32))
  (define X (binary-op "arith.subf"
                       (result (create-op "arith.sitofp"
                                          (loc: 0)
                                          (operands: I)
                                          (attributes:)
                                          (result-types: f32)))
                       (constant "1.0" f32)))
  (define P (result (create-op "vector.from_elements"
                               (loc: 0)
                               (operands: (binary-op "arith.mulf" X Half)
                                          (binary-op "arith.subf" Half
                                            (binary-op "arith.mulf" X X))
                                          (constant "0.0" f32)
                                          (constant "1.0" f32))
                               (attributes:)
                               (result-types: !vec4))))
  (return P (constant "dense<[1.0, 0.0, 0.0, 1.0]>" !vec4)))

; // Write the colour from the vertex entry point.
(define-shader-entrypoint fs_color "Fragment" ((Color : !vec4)) -> (!vec4)
  (return Color))

(write-spirv-binary "shaders.spv")
(finalize-module)
} // schir_scheme
} // namespace foo
} // namespace

constexpr nbdl::shader_entry const& VsTriangle =
    nbdl::sdl3::find_entry(foo::shaders_entries, "vs_triangle");
constexpr nbdl::shader_entry const& FsColor =
    nbdl::sdl3::find_entry(foo::shaders_entries, "fs_color");
static_assert(VsTriangle.stage == nbdl::shader_stage::vertex);
static_assert(FsColor.stage == nbdl::shader_stage::fragment);

constexpr Uint32 Size = 64;
constexpr std::array<Uint8, 4> Clear = {0, 0, 255, 255};
constexpr std::array<Uint8, 4> Red = {255, 0, 0, 255};

int main() {
  // Run without a window via a headless surface.
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  SCHIR_ASSERT(SDL_Init(SDL_INIT_VIDEO));
  SDL_GPUDevice* Device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
  SCHIR_ASSERT(Device != nullptr);
  // Print the device that runs the shaders.
  SDL_PropertiesID Props = SDL_GetGPUDeviceProperties(Device);
  std::printf("device: %s (%s %s)\n",
              SDL_GetStringProperty(Props, SDL_PROP_GPU_DEVICE_NAME_STRING,
                                    "unknown"),
              SDL_GetStringProperty(Props,
                                    SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,
                                    "unknown"),
              SDL_GetStringProperty(Props,
                                    SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,
                                    ""));

  // A compute pipeline cannot be made from a vertex entry point.
  SCHIR_ASSERT(nbdl::sdl3::create_compute_pipeline(Device, foo::shaders_get_program(),
                                                   VsTriangle) == nullptr);

  SDL_GPUShader* VertexShader =
      nbdl::sdl3::create_shader(Device, foo::shaders_get_program(), VsTriangle);
  SDL_GPUShader* FragmentShader =
      nbdl::sdl3::create_shader(Device, foo::shaders_get_program(), FsColor);
  SCHIR_ASSERT(VertexShader != nullptr && FragmentShader != nullptr);

  // Draw without culling so the winding and y direction do not matter.
  SDL_GPUColorTargetDescription TargetDesc{};
  TargetDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  SDL_GPUGraphicsPipelineCreateInfo PipelineInfo =
      nbdl::sdl3::graphics_pipeline_create_info(VertexShader, FragmentShader);
  PipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  PipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  PipelineInfo.target_info.color_target_descriptions = &TargetDesc;
  PipelineInfo.target_info.num_color_targets = 1;
  SDL_GPUGraphicsPipeline* Pipeline =
      SDL_CreateGPUGraphicsPipeline(Device, &PipelineInfo);
  SCHIR_ASSERT(Pipeline != nullptr);
  SDL_ReleaseGPUShader(Device, FragmentShader);
  SDL_ReleaseGPUShader(Device, VertexShader);

  SDL_GPUTextureCreateInfo TextureInfo{};
  TextureInfo.type = SDL_GPU_TEXTURETYPE_2D;
  TextureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  TextureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  TextureInfo.width = Size;
  TextureInfo.height = Size;
  TextureInfo.layer_count_or_depth = 1;
  TextureInfo.num_levels = 1;
  SDL_GPUTexture* Target = SDL_CreateGPUTexture(Device, &TextureInfo);
  SCHIR_ASSERT(Target != nullptr);

  SDL_GPUTransferBufferCreateInfo DownloadInfo{};
  DownloadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  DownloadInfo.size = Size * Size * 4;
  SDL_GPUTransferBuffer* Download =
      SDL_CreateGPUTransferBuffer(Device, &DownloadInfo);
  SCHIR_ASSERT(Download != nullptr);

  SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Device);
  SCHIR_ASSERT(Commands != nullptr);

  SDL_GPUColorTargetInfo ColorTarget{};
  ColorTarget.texture = Target;
  ColorTarget.clear_color = {Clear[0] / 255.0f, Clear[1] / 255.0f,
                             Clear[2] / 255.0f, Clear[3] / 255.0f};
  ColorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  ColorTarget.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* Render =
      SDL_BeginGPURenderPass(Commands, &ColorTarget, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(Render, Pipeline);
  SDL_DrawGPUPrimitives(Render, 3, 1, 0, 0);
  SDL_EndGPURenderPass(Render);

  SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Commands);
  SDL_GPUTextureRegion Region{};
  Region.texture = Target;
  Region.w = Size;
  Region.h = Size;
  Region.d = 1;
  SDL_GPUTextureTransferInfo DownloadTo{};
  DownloadTo.transfer_buffer = Download;
  SDL_DownloadFromGPUTexture(Copy, &Region, &DownloadTo);
  SDL_EndGPUCopyPass(Copy);

  SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
  SCHIR_ASSERT(Fence != nullptr);
  SCHIR_ASSERT(SDL_WaitForGPUFences(Device, true, &Fence, 1));
  SDL_ReleaseGPUFence(Device, Fence);

  auto* Pixels =
      static_cast<Uint8*>(SDL_MapGPUTransferBuffer(Device, Download, false));
  SCHIR_ASSERT(Pixels != nullptr);
  auto PixelAt = [&](Uint32 X, Uint32 Y) {
    Uint8 const* P = Pixels + (Y * Size + X) * 4;
    return std::array<Uint8, 4>{P[0], P[1], P[2], P[3]};
  };
  SCHIR_ASSERT(PixelAt(0, 0) == Clear);
  SCHIR_ASSERT(PixelAt(Size / 2, Size / 2) == Red);
  SDL_UnmapGPUTransferBuffer(Device, Download);

  SDL_ReleaseGPUTransferBuffer(Device, Download);
  SDL_ReleaseGPUTexture(Device, Target);
  SDL_ReleaseGPUGraphicsPipeline(Device, Pipeline);
  SDL_DestroyGPUDevice(Device);
  SDL_Quit();
}
