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
#include <cstring>

// Run a compute entry point with runtime-sized storage buffers on a GPU via
// the SDL3 adapter and check its results.
namespace {
namespace foo {
#pragma schir_scheme
{
(import (nbdl spec)
        (nbdl spec spirv)
        (only (schir mlir) attr create-op load-dialect result))

(load-dialect "memref")
(export-shader scale_at cs_scale)
(export-spirv-binary shaders)

(define f32 (type "f32"))
(define i1 (type "i1"))
(define i32 (type "i32"))
(define index (type "index"))
(define !buf (type "memref<?xf32>"))

(define InvocationIndex (shader-global 'builtin "LocalInvocationIndex" i32))
(define Scale (shader-global 'uniform-buffer f32))
(define Src (shader-global 'storage-buffer !buf))
(define Dest (shader-global 'readwrite-storage-buffer !buf))

(define (index-constant Value)
  (result (create-op "arith.constant"
                     (loc: 0)
                     (operands:)
                     (attributes: ("value" (attr Value index)))
                     (result-types: index))))

(define (dim Buffer)
  (result (create-op "memref.dim"
                     (loc: 0)
                     (operands: Buffer (index-constant "0"))
                     (attributes:)
                     (result-types: index))))

(define (binary-op Name A B Type)
  (result (create-op Name
                     (loc: 0)
                     (operands: A B)
                     (attributes:)
                     (result-types: Type))))

; // Store S[I % M] * C into D[I].
(define-fn scale_at ((I : index) (M : index) (C : f32) (S : !buf) (D : !buf))
    -> ()
  (define X (result (create-op "memref.load"
                               (loc: 0)
                               (operands: S (binary-op "arith.remui" I M index))
                               (attributes:)
                               (result-types: f32))))
  (create-op "memref.store"
             (loc: 0)
             (operands: (binary-op "arith.mulf" X C f32) D I)
             (attributes:)
             (result-types:))
  (return))

; // Scale the source into each element of the destination that is in bounds.
(define-shader-entrypoint cs_scale ("Compute" 64 1 1)
    ((I : InvocationIndex) (C : Scale) (S : Src) (D : Dest)) -> ()
  (define J (result (create-op "arith.index_cast"
                               (loc: 0)
                               (operands: I)
                               (attributes:)
                               (result-types: index))))
  (define InBounds
    (result (create-op "arith.cmpi"
                       (loc: 0)
                       (operands: J (dim D))
                       (attributes: ("predicate" (attr "6" (type "i64"))))
                       (result-types: i1))))
  (match-if InBounds
    (visit scale_at J (dim S) C S D))
  (return))

(write-spirv-binary "shaders.spv")
(finalize-module)
} // schir_scheme
} // namespace foo
} // namespace

constexpr nbdl::shader_entry const& CsScale =
    nbdl::sdl3::find_entry(foo::shaders_entries, "cs_scale");
static_assert(CsScale.length_uniform_slot == 1);
static_assert(nbdl::sdl3::num_lengths(CsScale) == 2);

constexpr std::uint32_t NumSrc = 3;
constexpr std::uint32_t NumDest = 50;
// Leave the elements past the length of the destination as a sentinel.
constexpr std::uint32_t DestCapacity = 64;
constexpr float Sentinel = -1.0f;

int main() {
  // Run without a window via a headless surface.
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  SCHIR_ASSERT(SDL_Init(SDL_INIT_VIDEO));
  SDL_GPUDevice* Device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
  SCHIR_ASSERT(Device != nullptr);
  // Print the device that runs the shader.
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

  SDL_GPUComputePipeline* Pipeline =
      nbdl::sdl3::create_compute_pipeline(Device, foo::shaders(), CsScale);
  SCHIR_ASSERT(Pipeline != nullptr);

  // A vertex or fragment shader cannot be made from a compute entry point.
  SCHIR_ASSERT(nbdl::sdl3::create_shader(Device, foo::shaders(), CsScale)
               == nullptr);

  std::array<float, NumSrc> Src = {1.0f, 2.0f, 3.0f};
  std::array<float, DestCapacity> Dest;
  Dest.fill(Sentinel);

  SDL_GPUBufferCreateInfo SrcInfo{};
  SrcInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
  SrcInfo.size = sizeof(Src);
  SDL_GPUBuffer* SrcBuffer = SDL_CreateGPUBuffer(Device, &SrcInfo);
  SDL_GPUBufferCreateInfo DestInfo{};
  DestInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  DestInfo.size = sizeof(Dest);
  SDL_GPUBuffer* DestBuffer = SDL_CreateGPUBuffer(Device, &DestInfo);
  SCHIR_ASSERT(SrcBuffer != nullptr && DestBuffer != nullptr);

  SDL_GPUTransferBufferCreateInfo UploadInfo{};
  UploadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  UploadInfo.size = sizeof(Src) + sizeof(Dest);
  SDL_GPUTransferBuffer* Upload =
      SDL_CreateGPUTransferBuffer(Device, &UploadInfo);
  SDL_GPUTransferBufferCreateInfo DownloadInfo{};
  DownloadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  DownloadInfo.size = sizeof(Dest);
  SDL_GPUTransferBuffer* Download =
      SDL_CreateGPUTransferBuffer(Device, &DownloadInfo);
  SCHIR_ASSERT(Upload != nullptr && Download != nullptr);

  auto* Mapped =
      static_cast<char*>(SDL_MapGPUTransferBuffer(Device, Upload, false));
  SCHIR_ASSERT(Mapped != nullptr);
  std::memcpy(Mapped, Src.data(), sizeof(Src));
  std::memcpy(Mapped + sizeof(Src), Dest.data(), sizeof(Dest));
  SDL_UnmapGPUTransferBuffer(Device, Upload);

  SDL_GPUCommandBuffer* Commands = SDL_AcquireGPUCommandBuffer(Device);
  SCHIR_ASSERT(Commands != nullptr);

  SDL_GPUCopyPass* Copy = SDL_BeginGPUCopyPass(Commands);
  SDL_GPUTransferBufferLocation SrcFrom{Upload, 0};
  SDL_GPUBufferRegion SrcTo{SrcBuffer, 0, sizeof(Src)};
  SDL_UploadToGPUBuffer(Copy, &SrcFrom, &SrcTo, false);
  SDL_GPUTransferBufferLocation DestFrom{Upload, sizeof(Src)};
  SDL_GPUBufferRegion DestTo{DestBuffer, 0, sizeof(Dest)};
  SDL_UploadToGPUBuffer(Copy, &DestFrom, &DestTo, false);
  SDL_EndGPUCopyPass(Copy);

  // Push the uniform buffer and then the lengths in their order.
  float Scale = 10.0f;
  SDL_PushGPUComputeUniformData(Commands, 0, &Scale, sizeof(Scale));
  std::array<std::int32_t, 2> Lengths = {NumSrc, NumDest};
  SCHIR_ASSERT(nbdl::sdl3::push_lengths(Commands, CsScale, Lengths));
  // The number of lengths must match the entry point.
  SCHIR_ASSERT(!nbdl::sdl3::push_lengths(
      Commands, CsScale, std::span<std::int32_t const>(Lengths).first(1)));

  SDL_GPUStorageBufferReadWriteBinding DestBinding{};
  DestBinding.buffer = DestBuffer;
  SDL_GPUComputePass* Compute =
      SDL_BeginGPUComputePass(Commands, nullptr, 0, &DestBinding, 1);
  SDL_BindGPUComputePipeline(Compute, Pipeline);
  SDL_BindGPUComputeStorageBuffers(Compute, 0, &SrcBuffer, 1);
  SDL_DispatchGPUCompute(Compute, 1, 1, 1);
  SDL_EndGPUComputePass(Compute);

  Copy = SDL_BeginGPUCopyPass(Commands);
  SDL_GPUBufferRegion DestBack{DestBuffer, 0, sizeof(Dest)};
  SDL_GPUTransferBufferLocation DownloadTo{Download, 0};
  SDL_DownloadFromGPUBuffer(Copy, &DestBack, &DownloadTo);
  SDL_EndGPUCopyPass(Copy);

  SDL_GPUFence* Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(Commands);
  SCHIR_ASSERT(Fence != nullptr);
  SCHIR_ASSERT(SDL_WaitForGPUFences(Device, true, &Fence, 1));
  SDL_ReleaseGPUFence(Device, Fence);

  auto* Result =
      static_cast<float*>(SDL_MapGPUTransferBuffer(Device, Download, false));
  SCHIR_ASSERT(Result != nullptr);
  for (std::uint32_t I = 0; I < NumDest; ++I)
    SCHIR_ASSERT(Result[I] == Src[I % NumSrc] * Scale);
  for (std::uint32_t I = NumDest; I < DestCapacity; ++I)
    SCHIR_ASSERT(Result[I] == Sentinel);
  SDL_UnmapGPUTransferBuffer(Device, Download);

  SDL_ReleaseGPUTransferBuffer(Device, Download);
  SDL_ReleaseGPUTransferBuffer(Device, Upload);
  SDL_ReleaseGPUBuffer(Device, DestBuffer);
  SDL_ReleaseGPUBuffer(Device, SrcBuffer);
  SDL_ReleaseGPUComputePipeline(Device, Pipeline);
  SDL_DestroyGPUDevice(Device);
  SDL_Quit();
}
