// RUN: clang++ -std=c++26 -I %schir_module_path -I %S/Inputs -fsyntax-only -fplugin=SchirClang.so -Xclang -verify %s
// expected-no-diagnostics

// Copied mostly from libc++ implementation of declval
// The two overloads makes __decval dependent and thus decval dependent
// where the body would not be instantiated in an unevaluated context.
template <class _Tp>
_Tp&& __declval(int);
template <class _Tp>
_Tp __declval(long);

template <class _Tp>
decltype(__declval<_Tp>(0)) declval();
#if 0
{
  static_assert(!__is_same(_Tp, _Tp), "unevaluated context required")
}
#endif


template <typename T>
struct remove_const;

template <typename T>
struct remove_const<T const> {
  using type = T;
};

template <typename T>
using remove_const_t = remove_const<T>::type;

namespace {
template <typename ...>
struct check { };

namespace my {
  struct foo { };
}

template <int>
struct probe {
  template <typename ...>
  using apply = int;
};

template <template<typename...> typename probe, typename ...Ts>
struct make_probes {
  probe<Ts...> x_0;
  probe<char, Ts...> x_1;
  probe<long> x_2;
  probe<> x_3;
};

namespace my_2 {
struct foo { };

static check<> check_3;
static check<long> check_2;
static check<char, int, float, char, my::foo,
             remove_const_t<foo const>> check_1;
static check<int, float, char, my::foo,
             my_2::foo> check_0;

#pragma schir_scheme
{
(import (schir base)
        (schir clang))
 ; // Expect a list
 (define (write-check TemplateArgs)
  (write-lexer "check<")
  (if (pair? TemplateArgs)
    (let Loop ((List TemplateArgs))
      (let ((Cur (car List))
            (Next (cdr List)))
        (cond
          ((pair? Cur)
            (write-check Cur)) ; // Nested check<...>
          ((null? Cur)
            (write-check Cur)) ; // Nested check<>
          ((symbol? Cur)
            (write-lexer Cur))
          (else (error "what?")))
        (if (pair? Next)
          (begin
            (write-lexer ", ")
            (Loop Next))))))
  (write-lexer ">"))

(define result
 (template-probe
   'RAW_LOC
   "probe<0>::apply"
   "
    [](auto&& ...arg) {
      make_probes<probe<0>::apply, decltype(arg)...,
                  float, char, my::foo const&,
                  remove_const_t<foo const>>();
    }(declval<int>())
   "
   ))
(write-lexer "using CheckResults = ")
(write-check result)
(write-lexer ";")
}

} // namespace my_2
} // namespace

int main() {
  // Expect cvref qualifiers to be stripped.
  check<
    check<>,
    check<long>,
    check<char, int, float, char, my::foo, my_2::foo>,
    check<int, float, char, my::foo, my_2::foo>
  > TheCheck = my_2::CheckResults();
}
