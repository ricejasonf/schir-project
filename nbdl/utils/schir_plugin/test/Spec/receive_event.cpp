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

#include <nbdl/spec.hpp>
#include <boost/hana/not_equal.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <functional>
#include <string>

namespace {

namespace hana = boost::hana;

struct noncopyable {
  noncopyable() = default;
  noncopyable(noncopyable const&) = delete;
};

namespace html {
  // Key for event_data.
  inline constexpr struct event_data_t {} event_data;
  struct event_data_impl { int id; };
  inline constexpr auto make_event_data = [](int id) {
    return event_data_impl{id};
  };

  struct event_receiver_vals {
    event_receiver_vals(int id)
      : vals(id)
    { }

    event_data_impl vals;
  };
} // html

namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec))

  (export-cpp
    my_context
    some_event_handler
    receive_event
    send_event)

  (define (ref Store)
    (visit 'std::ref Store))

  (define html::event_data 'html::event_data)

  (define-store my_context (Id)
    (store-compose '.nocopy (store 'noncopyable))
    (store-compose '.id (store 'int (init-args: Id))))

  (match-params-fn some_event_handler (Ctx Fn)
    ; // FIXME Should not need .hidden_parent_ to support member name access
    (visit 'nbdl::assign
           (get Ctx '.hidden_parent_ '.id)
           (get Ctx html::event_data '.id))
    (visit Fn (get Ctx html::event_data '.id)))

  ;; // Simulate an existing use case where we decorate
  ;; // a store with an html::event_data key.
  ;; // EventHandler is probably an anonymous match-params-fn
  ;; // created by the dsl.
  (match-params-fn receive_event (Ctx EventData EventHandler Fn)
    (visit
      EventHandler
      (store-compose html::event_data ;; // Create local context object
                     EventData ;(ref EventData)
                     (ref Ctx))
      Fn))

  (match-params-fn send_event (Ctx Id Fn)
    (visit receive_event
           (ref Ctx)
           (visit 'html::make_event_data Id)
           'foo::some_event_handler
           Fn))

}
}  // namespace foo
}  // namespace

int main() {
  auto ctx = foo::my_context(5);
  SCHIR_ASSERT(ctx.id == 5);

  std::optional<bool> IsDiff1;
  std::optional<bool> IsDiff2;
  std::optional<bool> IsDiff3;

  foo::send_event(ctx, 42,
    [Id = ctx.id, &IsDiff1](int Result) { IsDiff1 = Result != Id; });
  SCHIR_ASSERT(ctx.id == 42);
  SCHIR_ASSERT(*IsDiff1 == true);

  foo::send_event(ctx, 42,
    [Id = ctx.id, &IsDiff2](int Result) { IsDiff2 = Result != Id; });
  SCHIR_ASSERT(ctx.id == 42);
  SCHIR_ASSERT(*IsDiff2 == false);

  foo::send_event(ctx, 43,
    [Id = ctx.id, &IsDiff3](int Result) { IsDiff3 = Result != Id; });
  SCHIR_ASSERT(ctx.id == 43);
  SCHIR_ASSERT(*IsDiff3 == true);
}
