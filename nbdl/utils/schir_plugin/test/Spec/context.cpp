// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

//
// Copyright Jason Rice 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <schir/SCHIR_ASSERT.h>
#include <memory>
#include <string>

namespace {
namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec) (nbdl spec context))

  (export-cpp
    point
    my_context)

  (define-struct point ()
    (member: '.x 'int (init-args: 1))
    (member: '.y 'int (init-args: 2)))

  ; // Declare members from a procedure called in a context body.
  (define (define-counters . Keys)
    (for-each (lambda (Key) (define-member Key 'int 0)) Keys))

  (define-context my_context (arg1 arg2 arg3)
    (define-member '.foo 'int 42)
    (define-member '.bar 'std::string "initial string value")
    (define-member '.baz 'int arg1)
    (define-member '.boo 'std::string arg2)
    ; // Forward a move-only formal.
    (define-member '.ptr "std::unique_ptr<int>" arg3)
    ; // Construct a member without init args.
    (define-member '.point point)
    ; // Bind a local name that is not a member.
    (define Key '.local_key)
    (define-member Key 'int 7)
    (define-counters '.count_a '.count_b))

  (finalize-module)
}
}  // namespace foo
}  // namespace

int main() {
  std::string boo = "this is a boo";
  auto ctx = foo::my_context(5, std::move(boo), std::make_unique<int>(3));
  SCHIR_ASSERT(ctx.foo == 42);
  SCHIR_ASSERT(ctx.bar == std::string("initial string value"));
  SCHIR_ASSERT(ctx.baz == 5);
  SCHIR_ASSERT(ctx.boo == std::string("this is a boo"));
  SCHIR_ASSERT(*ctx.ptr == 3);
  SCHIR_ASSERT(ctx.point.x == 1);
  SCHIR_ASSERT(ctx.point.y == 2);
  SCHIR_ASSERT(ctx.local_key == 7);
  SCHIR_ASSERT(ctx.count_a == 0);
  SCHIR_ASSERT(ctx.count_b == 0);
}
