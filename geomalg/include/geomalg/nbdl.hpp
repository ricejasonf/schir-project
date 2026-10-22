//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef GEOMALG_NBDL_HPP
#define GEOMALG_NBDL_HPP

#include <charconv>
#include <concepts>
#include <cstdint>
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
// A scalar converts implicitly from a float.
template <uint32_t Tag>
struct blade {
  float value;

  constexpr blade() = default;
  explicit(Tag != 0) constexpr blade(float v) : value(v) { }
};

// The coefficients are in the order of the basis vectors.
//  vec3{1, 2, 3}
template <typename ...BasisVector>
struct multivector {
  static constexpr unsigned size = sizeof...(BasisVector);
  nbdl::vec_f32<size> value;

  constexpr multivector() = default;

  template <std::convertible_to<float> ...Coeffs>
    requires (sizeof...(Coeffs) == size)
  constexpr multivector(Coeffs ...coeffs)
    : value{static_cast<float>(coeffs)...} { }

  explicit constexpr multivector(nbdl::vec_f32<size> v) : value(v) { }
};

template <typename ...BasisVector>
struct unit_vector {
  static constexpr unsigned size = sizeof...(BasisVector);
  nbdl::vec_f32<size> value;

  constexpr unit_vector() = default;

  template <std::convertible_to<float> ...Coeffs>
    requires (sizeof...(Coeffs) == size)
  constexpr unit_vector(Coeffs ...coeffs)
    : value{static_cast<float>(coeffs)...} { }

  explicit constexpr unit_vector(nbdl::vec_f32<size> v) : value(v) { }
};

// A versor V with V Ṽ = 1 (e.g. a normalized motor.)
template <typename ...Blade>
struct unit_multivector {
  static constexpr unsigned size = sizeof...(Blade);
  nbdl::vec_f32<size> value;

  constexpr unit_multivector() = default;

  template <std::convertible_to<float> ...Coeffs>
    requires (sizeof...(Coeffs) == size)
  constexpr unit_multivector(Coeffs ...coeffs)
    : value{static_cast<float>(coeffs)...} { }

  explicit constexpr unit_multivector(nbdl::vec_f32<size> v) : value(v) { }
};

// The vector space operations of multivectors with the same basis.
template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator+(multivector<BasisVector...> a, multivector<BasisVector...> b) {
  return multivector<BasisVector...>(a.value + b.value);
}

template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator-(multivector<BasisVector...> a, multivector<BasisVector...> b) {
  return multivector<BasisVector...>(a.value - b.value);
}

template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator-(multivector<BasisVector...> a) {
  return multivector<BasisVector...>(-a.value);
}

template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator*(float s, multivector<BasisVector...> a) {
  return multivector<BasisVector...>(s * a.value);
}

template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator*(multivector<BasisVector...> a, float s) {
  return multivector<BasisVector...>(a.value * s);
}

template <typename ...BasisVector>
constexpr multivector<BasisVector...>
operator/(multivector<BasisVector...> a, float s) {
  return multivector<BasisVector...>(a.value / s);
}

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
using uvec2 = unit_vector<e1, e2>;
using uvec3 = unit_vector<e1, e2, e3>;

// Instantiate the common types for use with c linkage declarations,
// and perform some sanity checks on their byte sizes.
static_assert(sizeof(float) == 4); // Expect 32-bit floats.
static_assert(sizeof(scalar) == sizeof(float));
static_assert(sizeof(e1) == sizeof(float));
static_assert(sizeof(e2) == sizeof(float));
static_assert(sizeof(e3) == sizeof(float));
static_assert(sizeof(no) == sizeof(float));
static_assert(sizeof(ni) == sizeof(float));
static_assert(sizeof(vec2) == 8);
static_assert(sizeof(vec3) == 16);
static_assert(sizeof(vec4) == 16);
static_assert(sizeof(vec5) == 32);

// Compare the coefficients of a and b within an absolute tolerance.
// (NaN coefficients are never near.)
template <uint32_t Tag>
constexpr bool near(blade<Tag> a, blade<Tag> b, float tol = 1e-5f) {
  float d = a.value - b.value;
  return d * d <= tol * tol;
}

// (Allow the implicit conversion of a float to a scalar.)
constexpr bool near(scalar a, scalar b, float tol = 1e-5f) {
  return near<0>(a, b, tol);
}

template <typename ...BasisVector>
constexpr bool near(multivector<BasisVector...> a,
                    multivector<BasisVector...> b, float tol = 1e-5f) {
  auto d = a.value - b.value;
  return __builtin_reduce_and(d * d <= tol * tol);
}
} // namespace geomalg

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
    std::string result = "!geomalg.blade<";
    char buf[16]{};
    auto [end, _] = std::to_chars(buf, buf + sizeof(buf), Tag);
    result.append(buf, end);
    result.push_back('>');
    return result;
  }
};

template <typename ...BasisVector>
struct get_mlir_type<geomalg::multivector<BasisVector...>> {
  static constexpr std::string apply() {
    std::string result = std::string("!geomalg.multivector<");
    (result.append((get_mlir_type<BasisVector>::apply() + ',')), ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};

template <typename ...BasisVector>
struct get_mlir_type<geomalg::unit_vector<BasisVector...>> {
  static constexpr std::string apply() {
    std::string result = std::string("!geomalg.unit_vector<");
    (result.append((get_mlir_type<BasisVector>::apply() + ',')), ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};

template <typename ...Blade>
struct get_mlir_type<geomalg::unit_multivector<Blade...>> {
  static constexpr std::string apply() {
    std::string result = std::string("!geomalg.unit_multivector<");
    (result.append((get_mlir_type<Blade>::apply() + ',')), ...);
    // Replace the trailing comma.
    result.back() = '>';
    return result;
  }
};
} // namespace nbdl

#endif // GEOMALG_NBDL_HPP
