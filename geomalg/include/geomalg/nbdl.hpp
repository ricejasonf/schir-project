//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef GEOMALG_NBDL_HPP
#define GEOMALG_NBDL_HPP

#include <string>

// Forward declare nbdl stuff.
namespace nbdl {
template <unsigned n>
using vec_f32 = float __attribute__((ext_vector_type(n)));

template <typename T>
struct get_mlir_type;
} // namespace nbdl

namespace geomalg {
struct zero {
  float value = 0.0;
};

// Geomalg.Blade is implicitly defined as "basis k-blade".
// See Geomalg.td.
template <uint32_t Tag>
struct blade {
  float value;
};

template <typename... BasisVector>
struct multivector {
  vec_f32<sizeof...(BasisVector)> value;
};

template <typename... BasisVector>
struct unit_vector {
  vec_f32<sizeof...(BasisVector)> value;
};
}

namespace nbdl {
template <>
struct get_mlir_type<geomalg::zero> {
  static std::string apply() {
    return std::string("!geomalg.zero");
  }
};

template <unsigned Tag>
struct get_mlir_type<geomalg::blade<Tag>> {
  static std::string apply() {
    return std::string("!geomalg.blade<") + std::to_string(Tag) + '>';
  }
};

template <typename... basis_vecs>
struct get_mlir_type<geomalg::multivector<basis_vecs...>> {
  static std::string apply() {
    std::string result = std::string("!geomalg.multivector<");
    (result.append((get_mlir_type<basis_vecs>::apply() + ',')) ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};

template <typename... basis_vecs>
struct get_mlir_type<geomalg::unit_vector<basis_vecs...>> {
  static std::string apply() {
    std::string result = std::string("!geomalg.unit_vector<");
    (result.append((get_mlir_type<basis_vecs>::apply() + ',')) ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};
} // namespace nbdl

#endif // GEOMALG_NBDL_HPP
