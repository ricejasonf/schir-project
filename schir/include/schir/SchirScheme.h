//===- SchirScheme.h - Classes for representing declarations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains opaque user interfaces for SchirScheme
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_SCHIRSCHEME_H
#define LLVM_SCHIR_SCHIRSCHEME_H

#include "schir/Lexer.h"
#include "schir/Source.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace schir {
class Context;
class SourceFileStorage;
class SourceManager;
class Value;
class Environment;
class FullSourceLocation;
using ModuleLoadNamesFn = void(schir::Context&);
using ValueRefs   = llvm::MutableArrayRef<schir::Value>;
using ValueFnTy   = void (Context&, ValueRefs);

  namespace base {
    void eval(Context& C, ValueRefs Args);
  }

// SchirScheme - Opaque wrapper for schir::Context and common operations
//               needed for embedding scheme
class SchirScheme {
  friend class SourceFileStorage;

  std::unique_ptr<schir::Context> ContextPtr;
  std::unique_ptr<schir::SourceManager> SourceManagerPtr;
  std::unique_ptr<schir::SourceFileStorage,
                  void(*)(SourceFileStorage*)> SourceFileStoragePtr;

  schir::SourceManager& getSourceManager() {
    assert(SourceManagerPtr && "SchirScheme must be initialized");
    return *SourceManagerPtr;
  }

  schir::SourceFileStorage& getSourceFileStorage() {
    assert(SourceFileStoragePtr &&
        "InitSourceFileStorage must be called for "
        "optional file system support");
    return *SourceFileStoragePtr;
  }

  public:

  // LexerSpellings is used for holding the generated
  // code for the clang::Lexer spellings.
  std::unique_ptr<llvm::BumpPtrAllocator> LexerSpellings;

  SchirScheme(std::unique_ptr<schir::Context>);
  SchirScheme();
  ~SchirScheme();

  schir::Context& getContext() {
    assert(ContextPtr && "SchirScheme must be initialized");
    return *ContextPtr;
  }

  schir::FullSourceLocation getFullSourceLocation(schir::SourceLocation Loc);

  schir::Lexer createEmbeddedLexer(uintptr_t ExternalRawLoc,
                                   llvm::StringRef Name,
                                   char const* BufferStart,
                                   char const* BufferEnd,
                                   char const* BufferPos);
  schir::Value ParseSourceFile(schir::Lexer Lexer);
  schir::Value ParseSourceFile(schir::SourceLocation Loc,
                               llvm::StringRef Filename);
  schir::Value ParseSourceFile(uintptr_t ExternalRawLoc,
                               llvm::StringRef Name,
                               char const* BufferStart,
                               char const* BufferEnd,
                               char const* BufferPos);
  void InitSourceFileStorage();
  void SetIncludePaths(schir::Value IncludePaths);

  using ErrorHandlerFn = void(llvm::StringRef,
                              schir::FullSourceLocation const&);

  // ProcessTopLevelCommands
  //              - Reading tokens with the provided lexer, this command parses
  //                and evaluates a top level scheme command sequence returning
  //                true if there is an error
  //                The terminator token is only checked outside of expressions
  //                (ie a schir::tok::r_paren can terminate without effecting
  //                 the parsing of lists that are delimited by parens)
  //              - ExprHandler defaults to `base::eval`
  void ProcessTopLevelCommands(schir::Lexer& Lexer,
                               llvm::function_ref<ValueFnTy> ExprHandler,
                               llvm::function_ref<ErrorHandlerFn> ErrorHandler,
                               schir::tok Terminator = schir::tok::eof);
  // ProcessTopLevelCommands
  //              - Filename overload requires calling InitSourceFileStorage.
  void ProcessTopLevelCommands(llvm::StringRef Filename,
                               llvm::function_ref<ValueFnTy> ExprHandler,
                               llvm::function_ref<ErrorHandlerFn> ErrorHandler);

  // Registers a module import function
  void RegisterModule(llvm::StringRef MangledName,
                      schir::ModuleLoadNamesFn*);
};

}

#endif
