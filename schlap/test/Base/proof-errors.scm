; RUN: not schir-scheme -I %schir_module_path -I %schlap_module_path %s 2>&1 | FileCheck %s
#!curly-infix

(import (except (schir base) < > + - *)
        (schlap proof)
        (schlap infix))

;; Report a wrong proof term at its location under the theorem.
; CHECK: proof-errors.scm:[[@LINE+3]]:16: error: declaration does not check: contraposition
; CHECK: proof-errors.scm:[[@LINE+5]]:24: note: expected 'Q' but got 'P'
; CHECK-NEXT: (λ (P Q hpq hnq hp) (app hnq hp)))
(define-theorem contraposition
  (∀ ((P : Prop) (Q : Prop)) {{P ⇒ Q} ⇒ (¬ Q) ⇒ (¬ P)})
  ;; Apply hnq to a proof of P where it expects one of Q.
  (λ (P Q hpq hnq hp) (app hnq hp)))
