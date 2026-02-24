// GeomalgDialectPasses.td - MLIR Dialect Pass Definitions for Geomalg
//
// Copyright Jason Rice 2025
//
#ifndef GEOMALG_DIALECT_PASSES_TD
#define GEOMALG_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"

class GeomalgPass<string name, string operation = "">
: Pass<name, operation> {
  let dependentDialects = ["geomalg::GeomalgDialect",
                           "mlir::func::FuncDialect"];
}

class MetricPass<string name, string operation = "">
: GeomalgPass<name, operation> {
  let options = [
    Option<"metric", "metric", "geomalg::MetricKind",
           "geomalg::MetricKind::unknown",  // default
           "A metric determines the result of certain operations.",
           [{geomalg::getMetricKindEnumValues()}]>
  ];
}

def TypeInferencePass : MetricPass<"geomalg-type-inference",
                                   "mlir::func::FuncOp"> {
  let summary = "Substitute unknown types with inferred types where possible";
  let description = [{
    The results of operations and functions are initially constructed with the
    !geomalg.unknown type or some other more constrained placeholder type.
    We replace these with inferred types within this pass.
  }];
}

// TODO
def ArgumentDeductionPass : GeomalgPass<"geomalg-argument-deduction",
                                        "mlir::ModuleOp"> {
  let summary = "Inline or instantiate functions with placeholder types";
  let description = [{
    For any call to a function with placeholder types, inline the function or
    instantiate a new function with the placeholder types for the arguments
    substituted.
  }];
}

def ExpandPass : GeomalgPass<"geomalg-expand", "mlir::func::FuncOp"> {
  let summary = "distribute over sums and combine like terms";
  let description = [{
    Given a multilinear operation with any operand of a Multivector type,
    distribute the operation across the terms by either expanding the
    multivector or modifying the sum operands that construct the multivector.
    The resulting sum terms should be combined such that every term has a distinct
    type in sorted order.
  }];
}

#endif  // GEOMALG_DIALECT_PASSES_TD
