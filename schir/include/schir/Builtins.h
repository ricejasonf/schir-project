//===- Base.h - Base library functions for SchirScheme ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares values and functions for the (schir builtins) scheme library
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_BUILTINS_H
#define LLVM_SCHIR_BUILTINS_H

#include "schir/Context.h"
#include "schir/Value.h"
#include "llvm/ADT/SmallVector.h"

#define SCHIR_BASE_LIB                _SCHIRL5SschirL8Sbuiltins
#define SCHIR_BASE_LIB_(NAME)         _SCHIRL5SschirL8Sbuiltins ## NAME
#define SCHIR_BASE_LIB_STR            "_SCHIRL5SschirL8Sbuiltins"
#define SCHIR_BASE_LOAD_MODULE        SCHIR_BASE_LIB_(_load_module)
#define SCHIR_BASE_INIT               SCHIR_BASE_LIB_(_init)

#define SCHIR_IMPORT_VAR              "_SCHIR_import"
#define SCHIR_LOAD_MODULE_VAR         "_SCHIR_load_module"

#define SCHIR_BASE_VAR(NAME)          ::schir::builtins_var::NAME
#define SCHIR_BASE_VAR_STR(NAME)      SCHIR_BASE_VAR_STR__##NAME
#define SCHIR_BASE_VAR_STR__error     SCHIR_BASE_LIB_STR "V5Serror"
#define SCHIR_BASE_VAR_STR__source_loc \
  SCHIR_BASE_LIB_STR "V6Ssourcemi3Sloc"

// Forward declare vars that are used in the C++ codebase.
namespace schir::builtins_var {
extern schir::ContextLocal include_paths;
extern schir::ContextLocal parse_source_file;
extern schir::ExternFunction compile;
extern schir::ExternFunction eval;
extern schir::ExternFunction op_eval;
}

namespace schir::builtins {
void eval(Context& C, ValueRefs Args);
void import_(Context& C, ValueRefs Args);
void load_module(Context& C, ValueRefs Args);
}

extern "C" {
// Initialize the module for run-time independent of the compiler.
void SCHIR_BASE_INIT(schir::Context& Context);

// Initialize the module and loads lookup information
// for the compiler.
void SCHIR_BASE_LOAD_MODULE(schir::Context& Context);
}

namespace schir::builtins {
/* InitParseSourceFile
 *  - Provide a hook for including source files by
 *    setting parse-source-file.
 *  - Inside Fn, the user can call SchirScheme::ParseSourceFile
 *    to allow them to implement file lookup and storage
 *    and manage their own source locations.
 *  - Fn is a more documentative interface for the user
 *    that might not be interested in writing Syntax in C++.
 *  - Fn must still call C.Cont(..) or C.RaiseError(..).
 *
 *  using ParseSourceFileFn = void(schir::Context&,
 *                                 schir::SourceLocation,
 *                                 schir::String*);
 */
template <typename ParseSourceFileFn>
void InitParseSourceFile(schir::Context& C, ParseSourceFileFn Fn) {
  auto ParseFn = [Fn](schir::Context& C, schir::ValueRefs Args) {
    if (Args.size() != 2)
      return C.RaiseError("parse-source-file expects 2 arguments");
    schir::SourceLocation Loc = Args[0].getSourceLocation();
    auto* Filename = dyn_cast<schir::String>(Args[1]);
    if (!Filename)
      return C.RaiseError("expecting filename");
    Fn(C, Loc, Filename);
  };
  SCHIR_BASE_VAR(parse_source_file).set(C, C.CreateLambda(ParseFn));
}

}

namespace schir::detail {
// Declare utility functions for iterating UTF-8 characters.
class Utf8View {
public:
  llvm::StringRef Range;
  Utf8View(llvm::StringRef StrView)
    : Range(StrView)
  { }

  bool empty() const {
    return Range.empty();
  }

  std::pair<uint32_t, unsigned> decode_front() const;

  // Return a single schir::Char or nullptr on error.
  schir::Value drop_front() {
    auto [codepoint, length] = decode_front();
    if (length == 0) return nullptr;

    Range = Range.substr(length);
    return schir::Char(codepoint);
  }
};
void encode_utf8(uint32_t UnicodeScalarValue,
                 llvm::SmallVectorImpl<char> &Result);

// Convert to a hexadecimal string for
// writing character constants and escaped
// hexadecimal codes.
void encode_hex(uint32_t Code,
                llvm::SmallVectorImpl<char> &Result);

// from_hex
std::pair</*CodePoint=*/  uint32_t,
          /*IsError=*/    bool>
from_hex(llvm::StringRef Chars);

}

#endif
