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
constexpr std::string mlir_to_string(unsigned n) {
  std::string result;
  do {
    result.insert(result.begin(), static_cast<char>('0' + n % 10));
    n /= 10;
  } while (n != 0);
  return result;
}
} // namespace detail

// Get the MLIR type string of T as a constant expression
// since a std::string cannot escape constant evaluation.
template <typename T>
constexpr auto mlir_type_name() {
  constexpr std::size_t size = get_mlir_type<T>::apply().size();
  std::array<char, size> result{};
  std::string str = get_mlir_type<T>::apply();
  std::copy(str.begin(), str.end(), result.begin());
  return result;
}

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
    return std::string("vector<") + detail::mlir_to_string(n) + "xf32>";
  }
};
} // namespace nbdl

#endif // NBDL_SPEC_MLIR_HPP
