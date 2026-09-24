//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_SPEC_MLIR_HPP
#define NBDL_SPEC_MLIR_HPP

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
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
// Specializations must implement `apply` as constexpr.
template <typename T>
struct get_mlir_type {
  static constexpr std::string apply() = delete;
};

namespace detail {
// Temporarily, shim for get_mlir_type not
// returning a compile-time string.
template <typename T>
constexpr auto mlir_type_name() {
  constexpr std::size_t size = get_mlir_type<T>::apply().size();
  std::array<char, size> result{};
  std::string str = get_mlir_type<T>::apply();
  std::copy(str.begin(), str.end(), result.begin());
  return result;
}
} // namespace detail

template <>
struct get_mlir_type<int32_t> {
  static constexpr std::string apply() {
    return "i32";
  }
};

template <>
struct get_mlir_type<float> {
  static constexpr std::string apply() {
    return "f32";
  }
};

template <unsigned n>
struct get_mlir_type<vec_f32<n>> {
  static constexpr std::string apply() {
    std::string result = "vector<";
    char buf[16]{};
    auto [end, _] = std::to_chars(buf, buf + sizeof(buf), n);
    result.append(buf, end);
    result.append("xf32>");
    return result;
  }
};
} // namespace nbdl

#endif // NBDL_SPEC_MLIR_HPP
