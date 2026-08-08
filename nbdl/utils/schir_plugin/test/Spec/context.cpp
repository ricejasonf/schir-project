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
#include <string>

namespace {
namespace foo {
#pragma schir_scheme
{
  (import (nbdl spec))
  (define-context my_context (arg1 arg2)
   (member: '.foo 'int
    (init-args: 42))
    (member: '.bar 'std::string
     (init-args: "initial string value"))
    (member: '.baz 'int
     (init-args: arg1))
    (member: '.boo 'std::string
     (init-args: arg2)))
}
}  // namespace foo
}  // namespace

int main() {
  std::string boo = "this is a boo";
  auto ctx = foo::my_context(5, std::move(boo));
  SCHIR_ASSERT(ctx.foo == 42);
  SCHIR_ASSERT(ctx.bar == std::string("initial string value"));
  SCHIR_ASSERT(ctx.baz == 5);
  SCHIR_ASSERT(ctx.boo == std::string("this is a boo"));
}
