// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so \
// RUN:   -fpass-plugin=SchirLLVMPass.so \
// RUN:   %s -o %t
// RUN: %t

#include <nbdl/ext/std/unordered_map.hpp>
#include <nbdl/spec.hpp>
#include <nbdl/tags.hpp>
#include <schir/SCHIR_ASSERT.h>
#include <string>
#include <unordered_map>

namespace {
namespace foo {

using int_or_text = nbdl::variant<int, std::string>;

struct app {
  int_or_text either;
  std::unordered_map<int, int> items;
  int selected;
};

struct result {
  int value = 0;
  bool not_matched = false;
};

// Record the value or the else of a path match.
struct recorder {
  result* r;

  void operator()(int value) const { r->value = value; }
  void operator()(nbdl::not_matched) const { r->not_matched = true; }
};

#pragma schir_scheme
{
(import (schir base) (schir mlir) (nbdl spec))

(export-cpp match_either_int match_selected_item)

; // Select the int alternative of the variant.
(define-fn match_either_int ((S : 'foo::app) (Rec : 'foo::recorder)) -> ()
  (match-path (lift-store S)
              (type "!nbdl.path<
                       !nbdl.member_name<\"either\">,
                       !nbdl.match<!nbdl.cpp<\"int\">>>")
    (lambda (X) (visit Rec X))
    (lambda (X) (visit Rec X))))

; // Key the map with the value of a nested path.
(define-fn match_selected_item ((S : 'foo::app) (Rec : 'foo::recorder)) -> ()
  (match-path (lift-store S)
              (type "!nbdl.path<
                       !nbdl.member_name<\"items\">,
                       !nbdl.path<!nbdl.member_name<\"selected\">>,
                       !nbdl.match<!nbdl.cpp<\"int\">>>")
    (lambda (X) (visit Rec X))
    (lambda (X) (visit Rec X))))

(finalize-module)
}

} // namespace foo
} // namespace

int main() {
  {
    foo::app App{.either = 42};
    foo::result R;
    foo::match_either_int(App, foo::recorder{&R});
    SCHIR_ASSERT(R.value == 42 && !R.not_matched);
  }
  {
    foo::app App{.either = std::string("text")};
    foo::result R;
    foo::match_either_int(App, foo::recorder{&R});
    SCHIR_ASSERT(R.value == 0 && R.not_matched);
  }
  {
    foo::app App{.items = {{1, 10}, {2, 20}}, .selected = 2};
    foo::result R;
    foo::match_selected_item(App, foo::recorder{&R});
    SCHIR_ASSERT(R.value == 20 && !R.not_matched);
  }
  {
    foo::app App{.items = {{1, 10}}, .selected = 3};
    foo::result R;
    foo::match_selected_item(App, foo::recorder{&R});
    SCHIR_ASSERT(R.value == 0 && R.not_matched);
  }
}
