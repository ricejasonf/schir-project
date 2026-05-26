; RUN: schir-scheme --module-path=%schir_module_path %s | FileCheck %s
(import (schir builtins))

; CHECK: "no params"
(write
  (format-string "no params"))
(newline)

; CHECK: "no params: "
(write
  (format-string "no params: {}"))
(newline)

; CHECK: "this is a number: 5"
(write
  (format-string "this is a number: {}" 5))
(newline)

; CHECK: "this is a string: \"yo\""
(write
  (format-string "this is a string: {}" "yo"))
(newline)

; CHECK: "this is a number and a string: 5 \"yo\""
(write
  (format-string "this is a number and a string: {} {}" 5 "yo"))
(newline)

; CHECK: "this is a number and a string: 6 \"foo\""
(write
  (format-string "this is a number and a string: {} {}" 6 "foo"))
(newline)

; CHECK: "this is just a number: 5"
(write
  (format-string "this is just a number: {}" 5 "yo"))
(newline)

; CHECK: "this is still just a number: 5"
(write
  (format-string "this is still just a number: {0}" 5 "yo"))
(newline)

; CHECK: "this is just a string: \"yo\""
(write
  (format-string "this is just a string: {1}" 5 "yo"))
(newline)

; CHECK: "accessing invalid index: 3"
(write
  (format-string "accessing invalid index: {3}" 5 "yo"))
(newline)

; CHECK: "accessing invalid index: "
(write
  (format-string "accessing invalid index: {foo}" 5 "yo"))
(newline)

; CHECK: "escaping braces: {foo}"
(write
  (format-string "escaping braces: {{foo}" 5 "yo"))
(newline)
