// Passes.td - MLIR Pass Definitions for Schlap
//
// Copyright Jason Rice 2026
//
#ifndef SCHLAP_DIALECT_PASSES_TD
#define SCHLAP_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"

class SchlapPass<string name, string operation = "">
: Pass<name, operation> {
  let dependentDialects = ["schlap::SchlapDialect"];
}

def InferTypesPass : SchlapPass<"schlap-infer-types"> {
  let summary = "Replace unknown types with the types they are inferred to be.";
}

def NormalizeActionsPass : SchlapPass<"schlap-normalize-actions"> {
  let summary = "Rewrite next-state relations into an executable form.";
}

def CheckPass : SchlapPass<"schlap-check"> {
  let summary = "Check the invariants of specifications on every reachable state.";
  let options = [
    Option<"stateLimit", "state-limit", "uint64_t", /*default=*/"1000000",
           "Stop with an error after this many distinct states.">,
    Option<"checkDeadlock", "check-deadlock", "bool", /*default=*/"true",
           "Report a reachable state with no next state as an error.">,
    Option<"reportStates", "report-states", "bool", /*default=*/"false",
           "Report the number of distinct states as a remark.">
  ];
}

def CheckProofsPass : SchlapPass<"schlap-check-proofs"> {
  let summary = "Check that each proof term has the type of its proposition.";
}

def ApplyRulePass : SchlapPass<"schlap-apply-rule", "mlir::ModuleOp"> {
  let summary = "Apply each rule of a module to each function of the module.";
  let description = [{
    Create a specification of the paths through each function of the module
    for each schlap.rule of the module, at the end of the module. Treat
    a symbol operation with one region as a function whose body is that
    region.
  }];
}

#endif  // SCHLAP_DIALECT_PASSES_TD
