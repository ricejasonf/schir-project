//===----- Clang.h - SchirScheme module for clang bindings ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains an interface to initialize a Scheme module from Clang.
//  The names use SchirScheme's mangling to allow for a consistent interface
//  for loading modules from precompiled code.
//  Ideally files like this should be generated, but this serves as a reference.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_CLANG_H
#define LLVM_SCHIR_CLANG_H

#include "schir/Value.h"

// TODO All of this could be moved to SchirClang.cpp and schir/clang.sld.

// This is the mangled library name.
#define SCHIR_CLANG_LIB_STR "_SCHIRL5SschirL5Sclang"

#define SCHIR_CLANG_VAR(NAME) ::schir_clang::NAME
namespace schir_clang {
extern schir::ContextLocal diag_error;
extern schir::ContextLocal diag_warning;
extern schir::ContextLocal diag_note;
extern schir::ContextLocal hello_world;
extern schir::ContextLocal write_lexer;
extern schir::ContextLocal lexer_writer;
extern schir::ContextLocal expr_eval;
extern schir::ContextLocal expr_type;
extern schir::ContextLocal template_probe;
extern schir::ContextLocal flush_tokens;
extern schir::ContextLocal register_module;
extern schir::ContextLocal registered_modules;
}

extern "C" {
// Initialize the module and load lookup information
// for the compiler.
inline void SchirClangLoadModule(schir::Context& Context) {
  schir::initModuleNames(Context, SCHIR_CLANG_LIB_STR, {
    {"diag-error", schir_clang::diag_error.get(Context)},
    {"diag-warning", schir_clang::diag_warning.get(Context)},
    {"diag-note", schir_clang::diag_note.get(Context)},
    {"hello-world", schir_clang::hello_world.get(Context)},
    {"write-lexer", schir_clang::write_lexer.get(Context)},
    {"lexer-writer", schir_clang::lexer_writer.get(Context)},
    {"expr-eval", schir_clang::expr_eval.get(Context)},
    {"expr->type", schir_clang::expr_type.get(Context)},
    {"template-probe", schir_clang::template_probe.get(Context)},
    {"flush-tokens", schir_clang::flush_tokens.get(Context)},
    {"register-module", schir_clang::register_module.get(Context)}
    {"registered-modules", schir_clang::registered_modules.get(Context)}
  });
}
}

#endif
