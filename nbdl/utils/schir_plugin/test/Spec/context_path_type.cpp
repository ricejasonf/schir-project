// RUN: clang++ -std=c++26 -I %schir_module_path -I %nbdl_module_path \
// RUN:   -fplugin=SchirClang.so -fsyntax-only %s | FileCheck %s

//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/spec.hpp>

namespace foo {
#pragma schir_scheme
{
  (import (schir base) (schir mlir) (nbdl spec) (nbdl spec context))

  (export-cpp app)

  (define-context app ()
    (define-member '.rooms 'int)
    (define-member '.ui 'int))

  (define (show X)
    (write X)
    (newline))

  (define (GetRoomName Ctx)
    (get Ctx '.rooms (get Ctx '.ui '.selected) '.name))

  (define-match-fn show_path_types (Ctx Fn)
    ; // CHECK: !nbdl.path
    (show (path-type (get Ctx)))

    ; // CHECK-NEXT: !nbdl.path<!nbdl.member_name<"rooms">, !nbdl.path<!nbdl.member_name<"ui">, !nbdl.member_name<"selected">>, !nbdl.member_name<"name">>
    (show (path-type (GetRoomName Ctx)))

    ; // CHECK-NEXT: !nbdl.path<!nbdl.member_name<"rooms">, !nbdl.tag<!nbdl.cpp_alias<"std::remove_cvref_t<decltype(foo::tag)>">>>
    (show (path-type (get Ctx '.rooms (tag 'foo::tag))))

    ; // Round trip the printed form.
    ; // CHECK-NEXT: #t
    (show (eqv? (path-type (GetRoomName Ctx))
                (type "!nbdl.path<!nbdl.member_name<\"rooms\">, !nbdl.path<!nbdl.member_name<\"ui\">, !nbdl.member_name<\"selected\">>, !nbdl.member_name<\"name\">>")))
    (visit Fn Ctx))

  (define (path-string . Keys)
    (if (null? Keys)
      "!nbdl.path"
      (string-append "!nbdl.path<" (car Keys)
                     (apply string-append
                            (map (lambda (Key) (string-append ", " Key))
                                 (cdr Keys)))
                     ">")))
  (define (app-path . Keys)
    (type (apply path-string Keys)))
  (define rooms "!nbdl.member_name<\"rooms\">")
  (define name "!nbdl.member_name<\"name\">")
  (define ui "!nbdl.member_name<\"ui\">")
  (define (nested . Keys)
    (apply path-string Keys))

  ; // Nested keys may be equal at runtime.
  ; // CHECK-NEXT: #t
  (show (path-prefix? (app-path rooms (nested ui "!nbdl.member_name<\"a\">"))
                      (app-path rooms (nested ui "!nbdl.member_name<\"b\">") name)))
  ; // CHECK-NEXT: #f
  (show (path-prefix? (app-path rooms name) (app-path rooms)))
  ; // CHECK-NEXT: #f
  (show (path-prefix? (app-path rooms (nested ui)) (app-path rooms name)))
  ; // CHECK-NEXT: #t
  (show (paths-may-alias? (app-path rooms name) (app-path rooms)))
  ; // CHECK-NEXT: #f
  (show (paths-may-alias? (app-path rooms) (app-path ui)))

  (finalize-module)
}
}  // namespace foo
