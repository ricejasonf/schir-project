// GeomalgDialectPasses.td - MLIR Dialect Pass Definitions for Geomalg
//
// Copyright Jason Rice 2025
//
#ifndef GEOMALG_DIALECT_PASSES_TD
#define GEOMALG_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"
include "mlir/Rewrite/PassUtil.td"

class GeomalgPass<string name, string operation = "">
: Pass<name, operation> {
  let dependentDialects = ["geomalg::GeomalgDialect",
                           "mlir::func::FuncDialect"];
}

// Allow pass to optionally use one of the predefined metrics.
def MetricOption : 
    Option<"metric", "metric", "geomalg::MetricKind",
           "geomalg::MetricKind::unknown",  // default
           "A metric determines the result of certain operations.",
           [{geomalg::getMetricKindEnumValues()}]>;

def ExpandPass : GeomalgPass<"geomalg-expand", "mlir::func::FuncOp"> {
  let summary = "expand and simplify operations for geometric algebra";
  let description = [{
    Expand and simplify Geomalg operations by distribution of multilinear
    operations over multivectors and expansion of k-blades until they are
    all operations on 1-blades or scalars. If a metric is specified then
    it will resolve the inner product for specified entries or by assuming
    orthonormal 1-blades.
  }];

  let options =  [MetricOption] # RewritePassUtils.options;
}

def FuncInstPass : GeomalgPass<"geomalg-func-inst", "mlir::ModuleOp"> {
  let summary = "Deduce function return types and argument types";
  let description = [{
    Some operations use placeholder types until certain configurations
    are known (e.g. a metric). This propagates to function return types
    so we must alter the function type in a module pass. This will also
    replace geomalg.return with func.return.

    NOTE: We are not currently using placeholder types as arguments,
          but should that be necessary, this is the place to put that.
  }];
}

#endif  // GEOMALG_DIALECT_PASSES_TD
