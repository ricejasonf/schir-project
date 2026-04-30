//===---------- schir_opt_main.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <schir/Dialect.h>
#include <schir/Dialect/Passes.h>
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include <string>

int main(int argc, char ** argv) {
  mlir::DialectRegistry DialectRegistry;
  DialectRegistry.insert<schir::SchirDialect>();
  DialectRegistry.insert<mlir::func::FuncDialect>();

  schir::registerStripGlobalBindingsPass();

  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "schir-scheme optimizer driver\n", DialectRegistry));
}
