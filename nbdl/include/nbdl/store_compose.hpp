//
// Copyright Jason Rice 2017
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NBDL_STORE_COMPOSE_HPP
#define NBDL_STORE_COMPOSE_HPP

#include <nbdl/apply_action.hpp>
#include <nbdl/concept/Store.hpp>
#include <nbdl/fwd/store_compose.hpp>
#include <nbdl/match.hpp>

#include <boost/hana/core/is_a.hpp>
#include <type_traits>
#include <utility>

namespace nbdl {
  namespace hana = boost::hana;

  namespace detail {
    template <typename Key>
    struct store_composite_tag { };

    template <typename Key, typename Value, typename Parent>
      requires std::is_empty_v<Key>
    struct store_composite_t {
      using hana_tag = store_composite_tag<Key>;

      Parent hidden_parent_;
      Value hidden_value_;
    };

    template <typename Key>
    struct store_composite_action_tag { };

    template <typename Key, typename Action>
    struct store_composite_action_t {
      using hana_tag = store_composite_action_tag<Key>;

      Action action;
    };
  }

  template <typename Key, typename Value, typename Parent>
  constexpr auto store_compose_fn::operator()(Key, Value&& v,
                                              Parent&& p) const {
    using P = std::decay_t<Parent>;
    using V = std::decay_t<Value>;

    return detail::store_composite_t<Key, V, P>{
      std::forward<Parent>(p),
      std::forward<Value>(v)
    };
  }

  template <typename Key, typename Action>
  constexpr auto store_composite_action_fn::operator()(Key, Action&& a) const {
    return detail::store_composite_action_t<Key, std::decay_t<Action>>{
      std::forward<Action>(a)
    };
  }

  // apply_action_impl
  template <typename Key>
  struct apply_action_impl<detail::store_composite_tag<Key>> {
    template <typename Store, typename Action>
    static constexpr auto apply(Store& s, Action&& a) {
      if constexpr(
          hana::is_a<detail::store_composite_action_tag<Key>, Action>) {
        return nbdl::apply_action(s.hidden_value_, std::forward<Action>(a).action);
      } else {
        return nbdl::apply_action(s.hidden_parent_, std::forward<Action>(a));
      }
    }
  };

  // match_impl
  template <typename Key>
  struct match_impl<detail::store_composite_tag<Key>> {
    template <typename Store, typename KeyArg, typename Fn>
    static constexpr void apply(Store& s, KeyArg&& k, Fn&& fn) {
      if constexpr(std::is_same<Key, std::decay_t<KeyArg>>::value) {
        if constexpr(nbdl::Store<decltype(s.hidden_value_)>) {
          nbdl::match(s.hidden_value_, std::forward<Fn>(fn));
        } else {
          std::forward<Fn>(fn)(s.hidden_value_);
        }
      } else {
        nbdl::match(s.hidden_parent_, std::forward<KeyArg>(k),
                    std::forward<Fn>(fn));
      }
    }
  };
}

#endif
