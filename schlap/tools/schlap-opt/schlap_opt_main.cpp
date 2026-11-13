#include <schlap/Dialect.h>
#include <schlap/Passes.h>
#include <TestDialect.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Tools/mlir-opt/MlirOptMain.h>
#include <mlir/Transforms/Passes.h>

int main(int argc, char ** argv) {
  mlir::DialectRegistry DialectRegistry;
  DialectRegistry.insert<schlap::SchlapDialect>();
  DialectRegistry.insert<mlir::arith::ArithDialect>();
  DialectRegistry.insert<mlir::func::FuncDialect>();
  schlap_test::registerTestDialect(DialectRegistry);

  mlir::registerTransformsPasses();
  schlap::registerSchlapPasses();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "schlap optimizer driver\n", DialectRegistry));
}
