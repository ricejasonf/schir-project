//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_FWD_BIND_MEMREF_HPP
#define NBDL_FWD_BIND_MEMREF_HPP

#include <nbdl/concept/BindableMemref.hpp>

namespace nbdl {
  struct bind_memref_fn {
    template <BindableMemref BindableMemref>
    constexpr auto operator()(BindableMemref&&) const;
  };

  constexpr bind_memref_fn bind_memref{};
}

#endif // NBDL_FWD_BIND_MEMREF_HPP
