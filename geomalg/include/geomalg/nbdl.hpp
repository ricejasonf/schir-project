//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef GEOMALG_NBDL_HPP
#define GEOMALG_NBDL_HPP

#include <nbdl/spec/mlir.hpp>
#include <cstdint>
#include <string>

namespace geomalg {
using nbdl::vec_f32;

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

// Basis vectors and common types (See base.sld.)
using scalar = blade<0>;
using e1 = blade<1>;
using e2 = blade<2>;
using e3 = blade<4>;
using no = blade<8>;
using ni = blade<16>;

using vec2 = multivector<e1, e2>;
using vec3 = multivector<e1, e2, e3>;
using vec4 = multivector<e1, e2, e3, no>;
using vec5 = multivector<e1, e2, e3, no, ni>;
}

namespace nbdl {
template <>
struct get_mlir_type<geomalg::zero> {
  static constexpr std::string apply() {
    return std::string("!geomalg.zero");
  }
};

template <uint32_t Tag>
struct get_mlir_type<geomalg::blade<Tag>> {
  static constexpr std::string apply() {
    return std::string("!geomalg.blade<") + detail::mlir_to_string(Tag) + '>';
  }
};

template <typename... basis_vecs>
struct get_mlir_type<geomalg::multivector<basis_vecs...>> {
  static constexpr std::string apply() {
    std::string result = std::string("!geomalg.multivector<");
    (result.append((get_mlir_type<basis_vecs>::apply() + ',')), ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};

template <typename... basis_vecs>
struct get_mlir_type<geomalg::unit_vector<basis_vecs...>> {
  static constexpr std::string apply() {
    std::string result = std::string("!geomalg.unit_vector<");
    (result.append((get_mlir_type<basis_vecs>::apply() + ',')), ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};
} // namespace nbdl

#endif // GEOMALG_NBDL_HPP
