// RUN: schlap-opt %s -verify-diagnostics -schlap-infer-types \
// RUN:   -schlap-normalize-actions -schlap-check

// Report the six-step solution of the jugs puzzle as a violation.
schlap.spec @DieHard {
  %small = schlap.variable "small" : !schlap.unknown
  %big = schlap.variable "big" : !schlap.unknown
  // expected-note@+1 {{initial state: small = 0, big = 0}}
  schlap.init {
    %0 = schlap.int 0 : !schlap.unknown
    %1 = schlap.eq %small, %0 : !schlap.unknown
    %2 = schlap.eq %big, %0 : !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  schlap.action @FillSmall {
    %0 = schlap.int 3 : !schlap.unknown
    %p = schlap.prime %small : !schlap.unknown
    %1 = schlap.eq %p, %0 : !schlap.unknown
    %2 = schlap.unchanged %big : (!schlap.unknown) -> !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  // expected-note@+2 {{step 1, FillBig: small = 0, big = 5}}
  // expected-note@+1 {{step 5, FillBig: small = 2, big = 5}}
  schlap.action @FillBig {
    %0 = schlap.int 5 : !schlap.unknown
    %p = schlap.prime %big : !schlap.unknown
    %1 = schlap.eq %p, %0 : !schlap.unknown
    %2 = schlap.unchanged %small : (!schlap.unknown) -> !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  // expected-note@+1 {{step 3, EmptySmall: small = 0, big = 2}}
  schlap.action @EmptySmall {
    %0 = schlap.int 0 : !schlap.unknown
    %p = schlap.prime %small : !schlap.unknown
    %1 = schlap.eq %p, %0 : !schlap.unknown
    %2 = schlap.unchanged %big : (!schlap.unknown) -> !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  schlap.action @EmptyBig {
    %0 = schlap.int 0 : !schlap.unknown
    %p = schlap.prime %big : !schlap.unknown
    %1 = schlap.eq %p, %0 : !schlap.unknown
    %2 = schlap.unchanged %small : (!schlap.unknown) -> !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  schlap.action @SmallToBig {
    %c5 = schlap.int 5 : !schlap.unknown
    %room = schlap.sub %c5, %big : !schlap.unknown
    %lt = schlap.lt %small, %room : !schlap.unknown
    %n = schlap.ite %lt, %small, %room : !schlap.unknown
    %ps = schlap.prime %small : !schlap.unknown
    %pb = schlap.prime %big : !schlap.unknown
    %s = schlap.sub %small, %n : !schlap.unknown
    %b = schlap.add %big, %n : !schlap.unknown
    %1 = schlap.eq %ps, %s : !schlap.unknown
    %2 = schlap.eq %pb, %b : !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  // expected-note@+3 {{step 2, BigToSmall: small = 3, big = 2}}
  // expected-note@+2 {{step 4, BigToSmall: small = 2, big = 0}}
  // expected-note@+1 {{step 6, BigToSmall: small = 3, big = 4}}
  schlap.action @BigToSmall {
    %c3 = schlap.int 3 : !schlap.unknown
    %room = schlap.sub %c3, %small : !schlap.unknown
    %lt = schlap.lt %big, %room : !schlap.unknown
    %n = schlap.ite %lt, %big, %room : !schlap.unknown
    %ps = schlap.prime %small : !schlap.unknown
    %pb = schlap.prime %big : !schlap.unknown
    %s = schlap.add %small, %n : !schlap.unknown
    %b = schlap.sub %big, %n : !schlap.unknown
    %1 = schlap.eq %ps, %s : !schlap.unknown
    %2 = schlap.eq %pb, %b : !schlap.unknown
    %3 = schlap.and %1, %2 : !schlap.unknown
    schlap.yield %3 : !schlap.unknown
  }
  schlap.next {
    %a = schlap.action_ref @FillSmall : !schlap.unknown
    %b = schlap.action_ref @FillBig : !schlap.unknown
    %c = schlap.action_ref @EmptySmall : !schlap.unknown
    %d = schlap.action_ref @EmptyBig : !schlap.unknown
    %e = schlap.action_ref @SmallToBig : !schlap.unknown
    %f = schlap.action_ref @BigToSmall : !schlap.unknown
    %0 = schlap.or %a, %b : !schlap.unknown
    %1 = schlap.or %0, %c : !schlap.unknown
    %2 = schlap.or %1, %d : !schlap.unknown
    %3 = schlap.or %2, %e : !schlap.unknown
    %4 = schlap.or %3, %f : !schlap.unknown
    schlap.yield %4 : !schlap.unknown
  }
  // expected-error@+1 {{invariant NotSolved is violated after 6 steps}}
  schlap.invariant @NotSolved {
    %0 = schlap.int 4 : !schlap.unknown
    %1 = schlap.ne %big, %0 : !schlap.unknown
    schlap.yield %1 : !schlap.unknown
  }
}
