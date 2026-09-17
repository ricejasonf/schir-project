// RUN: clang++ -std=c++26 -I %schir_module_path -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics
#pragma schir_scheme
{
(import (schir base)
        (schir clang))
}

#include <array>
#include <format>
#include <string>
#include <string_view>

constexpr std::array<char, 5> greeting() {
  return {'h', 'e', 'l', 'l', 'o'};
}

constexpr std::array<char, 0> empty_array() {
  return {};
}

constexpr std::array<char, 50> repeated() {
  std::string piece = "ab";
  //std::string_view piece = "ab";
  int twelve = 12;
  std::array<char, 50> result{};
  char* itr = result.begin();
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < piece.size(); j++) {
      *itr = piece[j];
      ++itr;
    }
    auto [new_itr, _] = std::to_chars(itr, result.end(), twelve);
    itr = new_itr;
  }
  return result;
}

constexpr std::string_view sv_greeting() {
  return "hello view";
}

constexpr std::string_view empty_view() {
  return "";
}

// Exercise a nonzero LValue offset: substr() on a literal-backed
// string_view produces a view whose data() points partway into the
// original literal.
constexpr std::string_view sv_suffix() {
  std::string_view full = "hello view";
  return full.substr(6);
}

#pragma schir_scheme
{
  (assert-equal (expr-eval "greeting()") "hello")
  (assert-equal (expr-eval "empty_array()") "")
  (assert-equal (expr-eval "repeated()") "ab12ab12ab12ab12")
  (assert-equal (expr-eval "sv_greeting()") "hello view")
  (assert-equal (expr-eval "empty_view()") "")
  (assert-equal (expr-eval "sv_suffix()") "view")

  ; Non-string expressions should still evaluate as before.
  (assert-equal (expr-eval "41 + 1") 42)
} // pragma schir_scheme
