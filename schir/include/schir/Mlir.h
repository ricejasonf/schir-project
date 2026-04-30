//===- Mlir.h - Mlir binding functions for SchirScheme ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares values and functions for the (schir mlir) scheme library
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_MLIR_H
#define LLVM_SCHIR_MLIR_H

#define SCHIR_MLIR_LIB                _SCHIRL5SschirL4SmlirL8Sbuiltins
#define SCHIR_MLIR_LIB_(NAME)         _SCHIRL5SschirL4SmlirL8Sbuiltins ## NAME
#define SCHIR_MLIR_LIB_STR            "_SCHIRL5SschirL4SmlirL8Sbuiltins"
#define SCHIR_MLIR_LOAD_MODULE        SCHIR_MLIR_LIB_(_load_module)
#define SCHIR_MLIR_INIT               SCHIR_MLIR_LIB_(_init)

namespace schir {
class Context;
}

extern "C" {
// initialize the module for run-time independent of the compiler
void SCHIR_MLIR_LOAD_MODULE(schir::Context& Context);
void SCHIR_MLIR_INIT(schir::Context& Context);
}

#endif  // LLVM_SCHIR_MLIR_H
