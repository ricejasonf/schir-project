#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <mlir/Conversion/SPIRVToLLVM/SPIRVToLLVMPass.h>
#include <mlir/Dialect/Func/Transforms/Passes.h>
#include <mlir/Dialect/Linalg/Passes.h>
#include <mlir/Dialect/PDL/IR/PDL.h>
#include <mlir/Dialect/PDLInterp/IR/PDLInterp.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/Pass/PassOptions.h>
#include <mlir/Tools/mlir-opt/MlirOptMain.h>
#include <mlir/Transforms/Passes.h>
#include <string>

// Bypass including all of the conversion passes.
namespace mlir {
#define GEN_PASS_REGISTRATION_CONVERTSPIRVTOLLVMPASS
#include <mlir/Conversion/Passes.h.inc>
#undef GEN_PASS_REGISTRATION_CONVERTSPIRVTOLLVMPASS
}

int main(int argc, char ** argv) {
  mlir::DialectRegistry DialectRegistry;
  DialectRegistry.insert<geomalg::GeomalgDialect>();
  DialectRegistry.insert<mlir::func::FuncDialect>();
  DialectRegistry.insert<mlir::spirv::SPIRVDialect>();
  DialectRegistry.insert<mlir::vector::VectorDialect>();
  DialectRegistry.insert<mlir::pdl::PDLDialect>();
  DialectRegistry.insert<mlir::pdl_interp::PDLInterpDialect>();

  geomalg::registerGeomalgPasses();
  geomalg::registerGeomalgToSPIRV();
  geomalg::registerGeomalgToLLVM();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "geomalg optimizer driver\n", DialectRegistry));
}
