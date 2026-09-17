//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_SPEC_MLIR_HPP
#define NBDL_SPEC_MLIR_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace nbdl {
// Only support Clang.
template <unsigned n>
using vec_f32 = float __attribute__((ext_vector_type(n)));

// Create an injective map from a c++ type
// to a MLIR type via its string representation
// (ie to be parsed by MLIR.)
template <typename T>
struct get_mlir_type {
  static std::string apply() = delete;
};

template <>
struct get_mlir_type<int32_t> {
  static std::string apply() {
    return "i32";
  }
};

template <>
struct get_mlir_type<float> {
  static std::string apply() {
    return "f32";
  }
};

template <unsigned n>
struct get_mlir_type<vec_f32<n>> {
  static std::string apply() {
    return std::string("vector<") + std::to_string(n) + "xf32>";
  }
};
} // namespace nbdl

#endif // NBDL_SPEC_MLIR_HPP
