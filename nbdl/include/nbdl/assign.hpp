//
// Copyright Jason Rice 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_ASSIGN_HPP
#define NBDL_ASSIGN_HPP

#include <utility>

namespace nbdl {
  inline constexpr struct assign_t {
    template <typename LHS, typename RHS>
    auto&& operator()(LHS&& lhs, RHS&& rhs) const {
      return lhs = std::forward<RHS>(rhs);
    }
  } assign;
}

#endif
