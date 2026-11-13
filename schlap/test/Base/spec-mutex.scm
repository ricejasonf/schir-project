; RUN: not schir-scheme -I %schir_module_path -I %schlap_module_path %s 2>&1 \
; RUN:   | FileCheck %s
#!curly-infix
(import (except (schir base) < > + - *)
        (schlap base)
        (schlap infix))

; Check Peterson's algorithm for two processes.
(define-spec Peterson
  (variables pc flag turn)
  (define Procs (set-of 0 1))
  (define (Other p) {1 - p})
  (define (At p l) {(@ pc p) = l})
  (define (Go p l) {(′ pc) = (update pc p l)})

  (init {pc = (↦ ((p ∈ Procs)) 'idle) ∧
         flag = (↦ ((p ∈ Procs)) ⊥) ∧
         turn = 0})

  (action Request
    (∃ ((p ∈ Procs))
      {(At p 'idle) ∧ (Go p 'set-turn) ∧
       (′ flag) = (update flag p ⊤) ∧ (unchanged turn)}))
  (action SetTurn
    (∃ ((p ∈ Procs))
      {(At p 'set-turn) ∧ (Go p 'wait) ∧
       (′ turn) = (Other p) ∧ (unchanged flag)}))
  (action Enter
    (∃ ((p ∈ Procs))
      {(At p 'wait) ∧ {(@ flag (Other p)) = ⊥ ∨ turn = p} ∧
       (Go p 'cs) ∧ (unchanged flag turn)}))
  (action Exit
    (∃ ((p ∈ Procs))
      {(At p 'cs) ∧ (Go p 'idle) ∧
       (′ flag) = (update flag p ⊥) ∧ (unchanged turn)}))

  (next {Request ∨ SetTurn ∨ Enter ∨ Exit})

  (invariant Mutex (¬ {(At 0 'cs) ∧ (At 1 'cs)})))

; CHECK: 20
(write (check Peterson))
(newline)

; Check the other flag and set one's own in two steps.
(define-spec Broken
  (variables pc flag)
  (define Procs (set-of 0 1))
  (define (Other p) {1 - p})
  (define (At p l) {(@ pc p) = l})
  (define (Go p l) {(′ pc) = (update pc p l)})

  (init {pc = (↦ ((p ∈ Procs)) 'idle) ∧ flag = (↦ ((p ∈ Procs)) ⊥)})

  (action Check
    (∃ ((p ∈ Procs))
      {(At p 'idle) ∧ (@ flag (Other p)) = ⊥ ∧ (Go p 'entering) ∧
       (unchanged flag)}))
  (action Enter
    (∃ ((p ∈ Procs))
      {(At p 'entering) ∧ (Go p 'cs) ∧ (′ flag) = (update flag p ⊤)}))
  (action Exit
    (∃ ((p ∈ Procs))
      {(At p 'cs) ∧ (Go p 'idle) ∧ (′ flag) = (update flag p ⊥)}))

  (next {Check ∨ Enter ∨ Exit})

  (invariant Mutex (¬ {(At 0 'cs) ∧ (At 1 'cs)})))

; CHECK: error: check failed for spec: Broken
; CHECK: spec-mutex.scm:[[@LINE-3]]:{{[0-9]+}}: note: invariant Mutex is violated after 4 steps
; CHECK: note: initial state: pc = [0 ↦ idle, 1 ↦ idle], flag = [0 ↦ ⊥, 1 ↦ ⊥]
; CHECK: note: step 1, Check: pc = [0 ↦ entering, 1 ↦ idle], flag = [0 ↦ ⊥, 1 ↦ ⊥]
; CHECK: note: step 2, Check: pc = [0 ↦ entering, 1 ↦ entering], flag = [0 ↦ ⊥, 1 ↦ ⊥]
; CHECK: note: step 3, Enter: pc = [0 ↦ cs, 1 ↦ entering], flag = [0 ↦ ⊤, 1 ↦ ⊥]
; CHECK: note: step 4, Enter: pc = [0 ↦ cs, 1 ↦ cs], flag = [0 ↦ ⊤, 1 ↦ ⊤]
(check Broken)
