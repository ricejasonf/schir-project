#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/Linalg/Passes.h>
#include <mlir/Dialect/Bufferization/Transforms/Passes.h>
#include <mlir/Dialect/Affine/Passes.h>
#include <mlir/Dialect/Arith/Transforms/BufferizableOpInterfaceImpl.h>
#include <mlir/Dialect/Bufferization/Transforms/FuncBufferizableOpInterfaceImpl.h>
#include <mlir/Dialect/Linalg/Transforms/BufferizableOpInterfaceImpl.h>
#include <mlir/Dialect/MemRef/Transforms/AllocationOpInterfaceImpl.h>
#include <mlir/Dialect/Tensor/Transforms/BufferizableOpInterfaceImpl.h>
#include <mlir/Dialect/PDL/IR/PDL.h>
#include <mlir/Dialect/PDLInterp/IR/PDLInterp.h>
#include <mlir/InitAllPasses.h>
#include <mlir/Pass/PassOptions.h>
#include <mlir/Tools/mlir-opt/MlirOptMain.h>
#include <mlir/Transforms/Passes.h>
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
  DialectRegistry.insert<mlir::spirv::SPIRVDialect>();
  DialectRegistry.insert<mlir::pdl::PDLDialect>();
  DialectRegistry.insert<mlir::pdl_interp::PDLInterpDialect>();
  mlir::arith::registerBufferizableOpInterfaceExternalModels(DialectRegistry);
  mlir::bufferization
      ::func_ext
      ::registerBufferizableOpInterfaceExternalModels(DialectRegistry);
  mlir::linalg::registerBufferizableOpInterfaceExternalModels(DialectRegistry);
  mlir::tensor::registerBufferizableOpInterfaceExternalModels(DialectRegistry);
  mlir::tensor::registerBufferizableOpInterfaceExternalModels(DialectRegistry);
  mlir::memref::registerAllocationOpInterfaceExternalModels(DialectRegistry);

  geomalg::registerGeomalgPasses();
  //geomalg::registerGeomalgToSPIRV();

  // General passes
  mlir::registerCanonicalizerPass();
  mlir::registerCSEPass();
  mlir::registerMem2RegPass();
  mlir::registerSCCPPass();

  // Bufferization passes
  mlir::bufferization::registerOneShotBufferizePass();
  mlir::bufferization::registerPromoteBuffersToStackPassPass();
  mlir::bufferization::registerBufferResultsToOutParamsPass();

  // Linalg passes
  mlir::registerConvertLinalgToAffineLoopsPass();
  mlir::affine::registerAffinePasses();

  // FIXME This is temporary until we know the narrow set of passes
  //       necessary. This adds a significant amount to build times.
  mlir::registerAllPasses();

  mlir::PassPipelineRegistration<GeomalgFullExpandOptions>(
    "full-expand",
    "Full expand and optimize using a metric",
    [](mlir::OpPassManager& PM, GeomalgFullExpandOptions const& Opts) {
      llvm_unreachable("TODO");
    });

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "geomalg optimizer driver\n", DialectRegistry));
}
