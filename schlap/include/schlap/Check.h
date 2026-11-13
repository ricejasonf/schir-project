#ifndef SCHLAP_CHECK_H
#define SCHLAP_CHECK_H

#include <schlap/Dialect.h>
#include <llvm/Support/LogicalResult.h>
#include <cstdint>

namespace schlap {
// Configure checkSpec.
struct CheckOptions {
  // Stop with an error after this many distinct states.
  uint64_t StateLimit = 1000000;
  // Report a reachable state with no next state as an error.
  bool CheckDeadlock = true;
};

// Describe the state graph that checkSpec searched.
struct CheckStats {
  uint64_t NumStates = 0;
  uint64_t Depth = 0;
};

// Check the invariants of a normalized specification on every reachable
// state, emitting an error with the trace to the first violation.
llvm::FailureOr<CheckStats> checkSpec(SpecOp Spec,
                                      CheckOptions const& Options);
}  // namespace schlap

#endif  // SCHLAP_CHECK_H
