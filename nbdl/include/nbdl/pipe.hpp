//
// Copyright Jason Rice 2016
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_PIPE_HPP
#define NBDL_PIPE_HPP

#include <boost/hana/tuple.hpp>

namespace nbdl
{
  namespace hana = boost::hana;

  constexpr auto pipe = hana::make_tuple;
}

#endif
