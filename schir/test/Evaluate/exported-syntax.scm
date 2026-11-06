; RUN: rm -rf %t && mkdir -p %t/src/exported
; RUN: cp %S/Inputs/exported/syntax.sld %t/src/exported/
; RUN: schir-scheme --module-path=%schir_module_path --module-path=%t/src \
; RUN:   --prebuild-module-path=%t/bc %s | FileCheck %s
; RUN: rm %t/src/exported/syntax.sld
; RUN: schir-scheme --module-path=%schir_module_path --module-path=%t/src \
; RUN:   --prebuild-module-path=%t/bc %s | FileCheck %s

;; Run once from source and once from the prebuilt module alone.
(import (only (schir base) define lambda quote write newline
                            raise guard case let define-record-type)
        (exported syntax))

;; A template refers to a procedure and a variable the library does not export.
; CHECK: ((hidden-proc 1) hidden-var)
(write (use-hidden 1))
(newline)

;; A template refers to a name that only the library imports.
; CHECK-NEXT: (imported 2)
(write (use-import 2))
(newline)

;; A local binding of that name at the use site does not capture it.
; CHECK-NEXT: (imported 3)
((lambda (list) (write (use-import 3))) 'shadowed)
(newline)

;; A template uses an unexported syntax of the library.
; CHECK-NEXT: (hidden-syntax 4)
(write (use-hidden-syntax 4))
(newline)

;; An expansion defines a use-site name and hides a template name.
; CHECK-NEXT: 6user
((lambda ()
   (define hidden 'user)
   (define-getter get-six 6)
   (write (get-six))
   (write hidden)))
(newline)

;; A literal bound in the library matches the same binding.
; CHECK-NEXT: matchedno-match
(write (match-literal ⌋))
(write (match-literal 7))
(newline)

;; A recursive macro expands itself.
; CHECK-NEXT: 8
(write (my-or #f #f 8))
(newline)

;; A template expands to a syntax of a library the library imports.
; CHECK-NEXT: (caught 9)
(write (catch-raise 9))
(newline)

;; Syntax of (schir base) and the libraries it imports.
; CHECK-NEXT: boom
(write (guard (e (else e)) (raise 'boom)))
(newline)

; CHECK-NEXT: two
(write (case 2 ((1) 'one) ((2) 'two) (else 'other)))
(newline)

; CHECK-NEXT: 11
(write (let ((a 11)) a))
(newline)

; CHECK-NEXT: 12
(define-record-type <point>
  (make-point x y)
  point?
  (x point-x)
  (y point-y))
(write (point-x (make-point 12 13)))
(newline)
