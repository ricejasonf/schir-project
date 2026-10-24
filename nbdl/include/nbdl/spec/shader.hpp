//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_SPEC_SHADER_HPP
#define NBDL_SPEC_SHADER_HPP

#include <array>
#include <cstdint>

namespace nbdl {
enum class shader_stage {
  vertex,
  fragment,
  compute,
};

// Describe an entry point of a SPIRV binary exported via export-spirv-binary.
struct shader_entry {
  char const* name;
  shader_stage stage;
  std::uint32_t num_samplers;
  std::uint32_t num_storage_textures;
  std::uint32_t num_storage_buffers;
  std::uint32_t num_uniform_buffers;
  // Hold the local size of a compute shader or zeros.
  std::array<std::uint32_t, 3> threadcount;
};
} // namespace nbdl

#endif
