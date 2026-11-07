// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
namespace foo {
using names_map = std::unordered_map<std::string, std::string>;

// Key the value of a counter.
inline constexpr struct value_tag_t { } value_tag;

struct counter {
  int value = 3;
};
}  // namespace foo
}  // namespace

namespace nbdl {
template <>
struct match_impl<foo::counter> {
  template <typename Store, typename Fn>
  static constexpr void apply(Store&& S, Fn&& F) {
    std::forward<Fn>(F)(std::forward<Store>(S));
  }

  template <typename Store, typename Fn>
  static constexpr void apply(Store&& S, foo::value_tag_t, Fn&& F) {
    std::forward<Fn>(F)(S.value);
  }
};
}  // namespace nbdl

namespace {
namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec) (nbdl spec context))

  (export-cpp
    point
    app
    get_selected
    get_selected_name
    get_counter_value
    get_counter_value_by_get
    get_point_y)

  (define-struct point ()
    (member: '.x 'int (init-args: 1))
    (member: '.y 'int (init-args: 2)))

  (define-context app (Selected)
    (define-member '.selected 'std::string Selected)
    (define-member '.names 'foo::names_map)
    (define-member '.counter 'foo::counter)
    (define-member '.point point))

  ; // Define paths as procedures that extend a path with get.
  (define (GetSelected Ctx) (get Ctx '.selected))
  (define (GetSelectedName Ctx) (get Ctx '.names (GetSelected Ctx)))
  (define (GetCounterValue Ctx) (get Ctx '.counter (tag 'foo::value_tag)))
  (define (GetPoint Ctx) (get Ctx '.point))

  (define-match-fn get_selected (Ctx Fn)
    (visit Fn (GetSelected Ctx)))

  (define-match-fn get_selected_name (Ctx Fn)
    (match (GetSelectedName Ctx)
      ('std::string => (lambda (Name) (visit Fn Name)))
      (else => (lambda (NotFound) (visit Fn "not found")))))

  (define-match-fn get_counter_value (Ctx Fn)
    (visit Fn (GetCounterValue Ctx)))

  ; // Use a tag as a key without a path procedure.
  (define-match-fn get_counter_value_by_get (Ctx Fn)
    (visit Fn (get Ctx '.counter (tag 'foo::value_tag))))

  ; // Add keys after a path.
  (define-match-fn get_point_y (Ctx Fn)
    (visit Fn (get (GetPoint Ctx) '.y)))

  (finalize-module)
}
}  // namespace foo
}  // namespace

int main() {
  auto ctx = foo::app(std::string("b"));
  std::string Result;
  auto SetResult = [&](auto const& Name) { Result = std::string(Name); };

  foo::get_selected_name(ctx, SetResult);
  SCHIR_ASSERT(Result == "not found");

  ctx.names = {{"a", "Alice"}, {"b", "Bob"}};
  foo::get_selected_name(ctx, SetResult);
  SCHIR_ASSERT(Result == "Bob");

  ctx.selected = "a";
  foo::get_selected_name(ctx, SetResult);
  SCHIR_ASSERT(Result == "Alice");

  foo::get_selected(ctx, SetResult);
  SCHIR_ASSERT(Result == "a");

  int Value = 0;
  foo::get_counter_value(ctx, [&](int V) { Value = V; });
  SCHIR_ASSERT(Value == 3);

  Value = 0;
  foo::get_counter_value_by_get(ctx, [&](int V) { Value = V; });
  SCHIR_ASSERT(Value == 3);

  foo::get_point_y(ctx, [&](int V) { Value = V; });
  SCHIR_ASSERT(Value == 2);
}
