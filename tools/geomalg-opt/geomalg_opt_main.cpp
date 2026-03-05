#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <mlir/Dialect/PDL/IR/PDL.h>
#include <mlir/Dialect/PDLInterp/IR/PDLInterp.h>
#include <mlir/Pass/PassOptions.h>
#include <mlir/Tools/mlir-opt/MlirOptMain.h>
#include <string>

namespace {
struct GeomalgFullExpandOptions
        : public mlir::PassPipelineOptions<GeomalgFullExpandOptions> {
  Option<geomalg::MetricKind> MetricName{*this, "metric",
    llvm::cl::desc("A metric determines the result of certain operations"),
    geomalg::getMetricKindEnumValues()};
};
}

int main(int argc, char ** argv) {
  mlir::DialectRegistry DialectRegistry;
  DialectRegistry.insert<geomalg::GeomalgDialect>();
  DialectRegistry.insert<mlir::func::FuncDialect>();
  DialectRegistry.insert<mlir::pdl::PDLDialect>();
  DialectRegistry.insert<mlir::pdl_interp::PDLInterpDialect>();

  geomalg::registerGeomalgPasses();
  mlir::PassPipelineRegistration<GeomalgFullExpandOptions>(
    "full-expand",
    "Full expand and optimize using a metric",
    [](mlir::OpPassManager& PM, GeomalgFullExpandOptions const& Opts) {
      llvm_unreachable("TODO");
    });

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "geomalg optimizer driver\n", DialectRegistry));
}
