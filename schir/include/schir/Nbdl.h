//===- Nbdl.h - Nbdl binding functions for SchirScheme ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares values and functions for the (nbdl comp) scheme library
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_NBDL_H
#define LLVM_SCHIR_NBDL_H

#define SCHIR_NBDL_LIB                _SCHIRL4SnbdlL4Scomp
#define SCHIR_NBDL_LIB_(NAME)         _SCHIRL4SnbdlL4Scomp ## NAME
// (import (nbdl comp))
#define SCHIR_NBDL_LIB_STR            "_SCHIRL4SnbdlL4Scomp"
#define SCHIR_NBDL_LOAD_MODULE        SCHIR_NBDL_LIB_(_load_module)
#define SCHIR_NBDL_INIT               SCHIR_NBDL_LIB_(_init)

namespace schir {
class Context;
}

extern "C" {
// initialize the module for run-time independent of the compiler
void SCHIR_NBDL_LOAD_MODULE(schir::Context& Context);
void SCHIR_NBDL_INIT(schir::Context& Context);
}

#endif  // LLVM_SCHIR_NBDL_H
