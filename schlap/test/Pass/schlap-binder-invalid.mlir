// RUN: schlap-opt %s -split-input-file -verify-diagnostics \
// RUN:   -allow-unregistered-dialect

func.func @two_vars(%s: !schlap.unknown) {
  // expected-error@+1 {{'schlap.forall' op expects one bound variable}}
  %0 = "schlap.forall"(%s) ({
  ^bb0(%x: !schlap.unknown, %y: !schlap.unknown):
    "schlap.yield"(%x) : (!schlap.unknown) -> ()
  }) : (!schlap.unknown) -> !schlap.unknown
  return
}

// -----

func.func @no_yield(%s: !schlap.unknown) {
  // expected-error@+1 {{'schlap.exists' op expects the body to end with schlap.yield}}
  %0 = "schlap.exists"(%s) ({
  ^bb0(%x: !schlap.unknown):
    "test.terminator"() : () -> ()
  }) : (!schlap.unknown) -> !schlap.unknown
  return
}
