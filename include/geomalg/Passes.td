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
  let summary = "expand operations for geometric algebra";
  let description = [{
    Expand and simplify Geomalg operations by distribution of multilinear
    operations over multivectors and expansion of k-blades until they are
    all operations on 1-blades or scalars. If a metric is specified then
    it will resolve the inner product for specified entries or by assuming
    orthonormal 1-blades.
  }];

  let options =  [MetricOption] # RewritePassUtils.options;
}

def SimplifyPass : GeomalgPass<"geomalg-simplify", "mlir::func::FuncOp"> {
  let summary = "Simplify the geomalg expanded operations.";
  let description = [{
    Consolidate multiplications and negations, and group sums
    by type folding blade constants. The resulting IR should consist only of:

      expand, cmul, inverse, sum, negate, and possibly blade,

    but blade constants will likely be folded and/or cancelled.

    After this pass, blade types only indicate the values' destination in
    a the storage of a multivector.
  }];
  let options = RewritePassUtils.options;
}

def LowerPass : Pass<"geomalg-lower", "mlir::ModuleOp"> {
  let summary = "Lower Geomalg to Arith and Sparse dialects.";
  let description = [{
    Lower to dialects that themselves can lower to LLVM or SPIRV.
  }];
  let dependentDialects = ["geomalg::GeomalgDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::func::FuncDialect",
                           "mlir::linalg::LinalgDialect",
                           "mlir::tensor::TensorDialect"];
}

#endif  // GEOMALG_DIALECT_PASSES_TD
