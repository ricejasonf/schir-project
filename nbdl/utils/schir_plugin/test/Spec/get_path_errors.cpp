// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_LITERAL_KEY -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_OTHER_ROOT -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_MATCH_EACH -fsyntax-only -Xclang -verify %s
// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -DTEST_LOOP_CARRIED -fsyntax-only -Xclang -verify %s

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <unordered_map>

namespace {
namespace foo {
struct node {
  int count;
  node& next;
};

struct app {
  std::unordered_map<int, int> names;
};

struct msg {
  int id;
};
} // namespace foo
} // namespace

// Errors from the inference passes are raised by finalize-module
// with notes at the locations of the operations.
// expected-error@* {{nbdl inference passes failed}}

#ifdef TEST_LITERAL_KEY
// expected-note@+6 {{expecting a member name, a tag, or a store from the same root as a path key}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp literal_key)
(define-match-fn literal_key (Map Fn)
  (visit Fn (get-path (get Map 5))))
(finalize-module)
}
#endif

#ifdef TEST_OTHER_ROOT
// expected-note@+7 {{expecting a member name, a tag, or a store from the same root as a path key}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp other_root)
(define-match-fn other_root (Ctx Msg Fn)
  (match-params ((App : 'foo::app Ctx) (M : 'foo::msg Msg))
    (visit Fn (get-path (get App '.names (get M '.id))))))
(finalize-module)
}
#endif

#ifdef TEST_MATCH_EACH
// expected-note@+8 {{unable to resolve the key of a store in its parent}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp element_path)
(define-match-fn element_path (Range Fn)
  (match-each Range
    (lambda (Element)
      (visit Fn (get-path Element)))))
(finalize-module)
}
#endif

#ifdef TEST_LOOP_CARRIED
// expected-note@+7 {{path of store cannot be resolved in recursive context}}
#pragma schir_scheme
{
(import (nbdl spec))
(export-cpp path_of_loop_arg)
(define-match-fn path_of_loop (Node Fn)
  (match-if (visit '|std::equal_to<int>{}| (get Node '.count) '|int{0}|)
    (visit Fn (get-path Node))
    (visit path_of_loop (get Node '.next) Fn)))
(define-match-fn path_of_loop_arg (Node Fn)
  (visit path_of_loop Node Fn))
(finalize-module)
}
#endif
