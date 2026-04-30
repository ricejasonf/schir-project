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

#define SCHIR_CLANG_LIB               _SCHIRL5Sclang
#define SCHIR_CLANG_LIB_(NAME)        _SCHIRL5Sclang ## NAME
#define SCHIR_CLANG_LIB_STR           "_SCHIRL5SschirL5Sclang"
#define SCHIR_CLANG_LOAD_MODULE       SCHIR_CLANG_LIB##_load_module
#define SCHIR_CLANG_INIT              SCHIR_CLANG_LIB##_init
#define SCHIR_CLANG_VAR(NAME)         SCHIR_CLANG_VAR__##NAME
#define SCHIR_CLANG_VAR__diag_error   SCHIR_CLANG_LIB_(VS4diagmi5Serror)
#define SCHIR_CLANG_VAR__diag_warning SCHIR_CLANG_LIB_(VS4diagmi7Swarning)
#define SCHIR_CLANG_VAR__diag_note    SCHIR_CLANG_LIB_(VS4diagmi4Snote)
#define SCHIR_CLANG_VAR__hello_world  SCHIR_CLANG_LIB_(V5Shellomi5Sworld)
#define SCHIR_CLANG_VAR__write_lexer  SCHIR_CLANG_LIB_(V5Swritemi5Slexer)
#define SCHIR_CLANG_VAR__lexer_writer SCHIR_CLANG_LIB_(V5Slexermi6Swriter)
#define SCHIR_CLANG_VAR__expr_eval    SCHIR_CLANG_LIB_(V4Sexprmi4Seval)

// diag-error
// diag-warning
// diag-note
extern schir::ContextLocal SCHIR_CLANG_VAR(diag_error);
extern schir::ContextLocal SCHIR_CLANG_VAR(diag_warning);
extern schir::ContextLocal SCHIR_CLANG_VAR(diag_note);

// hello-world
extern schir::ContextLocal SCHIR_CLANG_VAR(hello_world);

// write-lexer
extern schir::ContextLocal SCHIR_CLANG_VAR(write_lexer);

// lexer-writer
extern schir::ContextLocal SCHIR_CLANG_VAR(lexer_writer);

// expr-eval
extern schir::ContextLocal SCHIR_CLANG_VAR(expr_eval);

extern "C" {
// initialize the module for run-time independent of the compiler
inline void SCHIR_CLANG_INIT(schir::Context& Context) {
  assert(SCHIR_CLANG_VAR(diag_error).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(diag_warning).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(diag_note).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(hello_world).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(write_lexer).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(lexer_writer).get(Context) &&
      "external module must be preloaded");
  assert(SCHIR_CLANG_VAR(expr_eval).get(Context) &&
      "external module must be preloaded");
}

// initializes the module and loads lookup information
// for the compiler
inline void SCHIR_CLANG_LOAD_MODULE(schir::Context& Context) {
  SCHIR_CLANG_INIT(Context);
  schir::initModuleNames(Context, SCHIR_CLANG_LIB_STR, {
    {"diag-error",  SCHIR_CLANG_VAR(diag_error).get(Context)},
    {"diag-warning",  SCHIR_CLANG_VAR(diag_warning).get(Context)},
    {"diag-note",  SCHIR_CLANG_VAR(diag_note).get(Context)},
    {"hello-world", SCHIR_CLANG_VAR(hello_world).get(Context)},
    {"write-lexer", SCHIR_CLANG_VAR(write_lexer).get(Context)},
    {"lexer-writer",SCHIR_CLANG_VAR(lexer_writer).get(Context)},
    {"expr-eval",   SCHIR_CLANG_VAR(expr_eval).get(Context)}
  });
}
}

#endif
