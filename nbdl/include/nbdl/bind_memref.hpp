//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_BIND_MEMREF_HPP
#define NBDL_BIND_MEMREF_HPP

#include <nbdl/fwd/bind_memref.hpp>

#include <nbdl/concept/BindableMemref.hpp>
#include <boost/hana/core/tag_of.hpp>
#include <mdspan>
#include <ranges>
#include <utility>

namespace nbdl {
  namespace hana = boost::hana;

  template <BindableMemref BindableMemref>
  constexpr auto bind_memref_fn::operator()(BindableMemref&& x) const {
    using Tag = hana::tag_of_t<BindableMemref>;
    using Impl = bind_memref_impl<Tag>;

    return Impl::apply(std::forward<BindableMemref>(x));
  }

  template <typename Tag>
  struct bind_memref_impl : default_impl {
    static constexpr auto apply(...) = delete;

    template <SizedContiguousRange C>
    static constexpr auto apply(C&& c) {
      using T = std::ranges::range_value_t<std::remove_cvref_t<C>>;
      return nbdl::memref<T, 1>{
        std::ranges::data(c), std::ranges::data(c), 0,
        static_cast<std::intptr_t>(std::ranges::size(c)),
        1
      };
    }
  };

  template <typename T, typename Extents, typename... Rest>
  struct bind_memref_impl<std::mdspan<T, Extents, Rest...>> {
    template <typename Span>
    static constexpr auto apply(Span&& m) {
      constexpr std::size_t Rank = Extents::rank();
      nbdl::memref<T, Rank> result{m.data_handle(), m.data_handle(),
                                   0, {}, {}};
      if constexpr (Rank == 1) {
        result.size = static_cast<std::intptr_t>(m.extent(0));
        result.stride = static_cast<std::intptr_t>(m.stride(0));
      } else {
        for (std::size_t i = 0; i < Rank; ++i) {
          result.sizes[i] = static_cast<std::intptr_t>(m.extent(i));
          result.strides[i] = static_cast<std::intptr_t>(m.stride(i));
        }
      }
      return result;
    }
  };
} // namespace nbdl

#endif // NBDL_BIND_MEMREF_HPP
