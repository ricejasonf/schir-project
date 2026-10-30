// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_ROOT -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_LOOP_CARRIED -fsyntax-only -Xclang -verify %s

#include <nbdl/spec.hpp>

namespace {
namespace foo {
struct node {
  int count;
  node& next;
};
} // namespace foo
} // namespace

// Errors from the inference passes are raised by finalize-module
// with notes at the locations of the operations.
// expected-error@* {{nbdl inference passes failed}}

#ifdef TEST_ROOT
// expected-note@+6 {{root store has no parent}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp parent_of_root)
(define-match-fn parent_of_root (Fn)
  (visit Fn (get-parent (lift-store 5))))
(finalize-module)
}
#endif

#ifdef TEST_LOOP_CARRIED
// expected-note@+8 {{parent of store cannot be resolved in recursive context}}
// expected-note@+8 {{the recursive visit is here}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp parent_of_loop_arg)
(define-match-fn parent_of_loop (Node Fn)
  (match-if (visit '|std::equal_to<int>{}| (get Node '.count) '|int{0}|)
    (visit Fn (get-parent Node))
    (visit parent_of_loop (get Node '.next) Fn)))
(define-match-fn parent_of_loop_arg (Node Fn)
  (visit parent_of_loop Node Fn))
(finalize-module)
}
#endif
