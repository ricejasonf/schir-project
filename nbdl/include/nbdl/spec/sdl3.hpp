//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_SPEC_SDL3_HPP
#define NBDL_SPEC_SDL3_HPP

#include <nbdl/memref.hpp>
#include <nbdl/spec/shader.hpp>
#include <SDL3/SDL_gpu.h>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace nbdl::sdl3 {
// Find the entry point with the given name in a table of entries.
constexpr shader_entry const& find_entry(std::span<shader_entry const> Entries,
                                         std::string_view Name) {
  for (shader_entry const& Entry : Entries) {
    if (std::string_view(Entry.name) == Name)
      return Entry;
  }
  throw std::out_of_range("nbdl::sdl3::find_entry: no such entry point");
}

// View the SPIRV words of a binary as read-only bytes.
inline std::span<Uint8 const> code_bytes(memref<std::int32_t, 1> Code) {
  return {reinterpret_cast<Uint8 const*>(Code.aligned + Code.offset),
          static_cast<std::size_t>(Code.size) * sizeof(std::int32_t)};
}

// Describe a vertex or fragment entry point for SDL_CreateGPUShader.
inline SDL_GPUShaderCreateInfo
shader_create_info(memref<std::int32_t, 1> Code, shader_entry const& Entry) {
  std::span<Uint8 const> Bytes = code_bytes(Code);
  SDL_GPUShaderCreateInfo Info{};
  Info.code_size = Bytes.size();
  Info.code = Bytes.data();
  Info.entrypoint = Entry.name;
  Info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  Info.stage = Entry.stage == shader_stage::vertex
                   ? SDL_GPU_SHADERSTAGE_VERTEX
                   : SDL_GPU_SHADERSTAGE_FRAGMENT;
  Info.num_samplers = Entry.num_samplers;
  Info.num_storage_textures = Entry.num_storage_textures;
  Info.num_storage_buffers = Entry.num_storage_buffers;
  Info.num_uniform_buffers = Entry.num_uniform_buffers;
  return Info;
}

// Describe a compute entry point for SDL_CreateGPUComputePipeline.
inline SDL_GPUComputePipelineCreateInfo
compute_pipeline_create_info(memref<std::int32_t, 1> Code,
                             shader_entry const& Entry) {
  std::span<Uint8 const> Bytes = code_bytes(Code);
  SDL_GPUComputePipelineCreateInfo Info{};
  Info.code_size = Bytes.size();
  Info.code = Bytes.data();
  Info.entrypoint = Entry.name;
  Info.format = SDL_GPU_SHADERFORMAT_SPIRV;
  Info.num_samplers = Entry.num_samplers;
  Info.num_readonly_storage_textures = Entry.num_storage_textures;
  Info.num_readonly_storage_buffers = Entry.num_storage_buffers;
  Info.num_readwrite_storage_textures = Entry.num_readwrite_storage_textures;
  Info.num_readwrite_storage_buffers = Entry.num_readwrite_storage_buffers;
  Info.num_uniform_buffers = Entry.num_uniform_buffers;
  Info.threadcount_x = Entry.threadcount[0];
  Info.threadcount_y = Entry.threadcount[1];
  Info.threadcount_z = Entry.threadcount[2];
  return Info;
}

// Describe a graphics pipeline with the given shaders and default state.
inline SDL_GPUGraphicsPipelineCreateInfo
graphics_pipeline_create_info(SDL_GPUShader* VertexShader,
                              SDL_GPUShader* FragmentShader) {
  SDL_GPUGraphicsPipelineCreateInfo Info{};
  Info.vertex_shader = VertexShader;
  Info.fragment_shader = FragmentShader;
  return Info;
}

// Create the shader of a vertex or fragment entry point.
inline SDL_GPUShader* create_shader(SDL_GPUDevice* Device,
                                    memref<std::int32_t, 1> Code,
                                    shader_entry const& Entry) {
  if (Entry.stage == shader_stage::compute) {
    SDL_SetError("nbdl::sdl3::create_shader: %s is a compute entry point",
                 Entry.name);
    return nullptr;
  }
  SDL_GPUShaderCreateInfo Info = shader_create_info(Code, Entry);
  return SDL_CreateGPUShader(Device, &Info);
}

// Create the pipeline of a compute entry point.
inline SDL_GPUComputePipeline*
create_compute_pipeline(SDL_GPUDevice* Device, memref<std::int32_t, 1> Code,
                        shader_entry const& Entry) {
  if (Entry.stage != shader_stage::compute) {
    SDL_SetError("nbdl::sdl3::create_compute_pipeline: "
                 "%s is not a compute entry point", Entry.name);
    return nullptr;
  }
  SDL_GPUComputePipelineCreateInfo Info =
      compute_pipeline_create_info(Code, Entry);
  return SDL_CreateGPUComputePipeline(Device, &Info);
}

// Count the lengths in the buffer of lengths of an entry.
constexpr std::size_t num_lengths(shader_entry const& Entry) {
  return static_cast<std::size_t>(
      std::popcount(Entry.length_storage_buffers) +
      std::popcount(Entry.length_readwrite_storage_buffers));
}

// Push the element counts of the runtime-sized storage buffers of an entry
// in the order of its buffer of lengths.
inline bool push_lengths(SDL_GPUCommandBuffer* CommandBuffer,
                         shader_entry const& Entry,
                         std::span<std::int32_t const> Counts) {
  if (Counts.size() != num_lengths(Entry)) {
    SDL_SetError("nbdl::sdl3::push_lengths: %s expects %zu lengths, got %zu",
                 Entry.name, num_lengths(Entry), Counts.size());
    return false;
  }
  if (Entry.length_uniform_slot < 0)
    return true;
  Uint32 Slot = static_cast<Uint32>(Entry.length_uniform_slot);
  Uint32 Size = static_cast<Uint32>(Counts.size_bytes());
  switch (Entry.stage) {
  case shader_stage::vertex:
    SDL_PushGPUVertexUniformData(CommandBuffer, Slot, Counts.data(), Size);
    break;
  case shader_stage::fragment:
    SDL_PushGPUFragmentUniformData(CommandBuffer, Slot, Counts.data(), Size);
    break;
  case shader_stage::compute:
    SDL_PushGPUComputeUniformData(CommandBuffer, Slot, Counts.data(), Size);
    break;
  }
  return true;
}
} // namespace nbdl::sdl3

#endif
