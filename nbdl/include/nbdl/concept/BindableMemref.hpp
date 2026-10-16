//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_CONCEPT_BINDABLE_MEMREF_HPP
#define NBDL_CONCEPT_BINDABLE_MEMREF_HPP

#include <nbdl/concept/extras.hpp>
#include <nbdl/concept/HasImpl.hpp>
#include <nbdl/memref.hpp>

#include <boost/hana/core/tag_of.hpp>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>

namespace nbdl {
  namespace hana = boost::hana;

  template <typename Tag>
  struct bind_memref_impl;

  template <typename T>
  concept SizedContiguousRange =
       std::ranges::contiguous_range<std::remove_cvref_t<T>>
    && std::ranges::sized_range<std::remove_cvref_t<T>>;

  template <typename T>
  concept BindableMemref = HasImpl<T, nbdl::bind_memref_impl>
                           || SizedContiguousRange<T>;
}

#endif
