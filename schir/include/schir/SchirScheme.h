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
#include <concepts>
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
    return *SourceManagerPtr;
  }

  schir::SourceFileStorage& getSourceFileStorage() {
    assert(SourceFileStoragePtr &&
        "InitSourceFileStorage must be called for "
        "optional file system support");
    return *SourceFileStoragePtr;
  }

  public:
  SchirScheme(std::unique_ptr<schir::Context>);
  SchirScheme(SchirScheme const&) = delete;
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

  using UserErrorHandlerFn = void(llvm::StringRef,
                                  schir::FullSourceLocation const&);
  std::function<UserErrorHandlerFn> UserErrorHandler;
  void RegisterErrorHandler(std::function<UserErrorHandlerFn> Fn);
  void RegisterErrorHandler(Lambda*);

  void ParseSourceFile(schir::Lexer Lexer);
  void ParseSourceFile(schir::SourceLocation Loc,
                       llvm::StringRef Filename);
  void ParseSourceFile(uintptr_t ExternalRawLoc,
                       llvm::StringRef Name,
                       char const* BufferStart,
                       char const* BufferEnd,
                       char const* BufferPos);
  void InitSourceFileStorage();
  void SetIncludePaths(schir::Value IncludePaths);

  // Enable the user to parse up front before processing.
  void ParseTopLevelCommands(schir::Lexer& Lexer,
                             schir::tok Terminator = schir::tok::eof);
  // Handle the parsed exprs which are saved in a context global.
  void ProcessPendingExprs(llvm::function_ref<ValueFnTy> ExprHandler);
  // Parse top level expressions and apply ExprHandler to the results.
  // The terminator token is only checked outside of expressions
  // (ie A schir::tok::r_paren can terminate without effecting
  //     the parsing of lists that are delimited by parens)
  void ProcessTopLevelCommands(schir::Lexer& Lexer,
                               llvm::function_ref<ValueFnTy> ExprHandler,
                               schir::tok Terminator = schir::tok::eof);
  // Filename overload requires calling InitSourceFileStorage.
  // (Implemented in SourceFileStorage.)
  void ProcessTopLevelCommands(llvm::StringRef Filename,
                               llvm::function_ref<ValueFnTy> ExprHandler);

  // Allow the user to break from evaluation saving the current
  // continuation to be called when they resume.
  void Break();
  // Resume evaluation after break point.
  bool Resume();

  // Registers a module import function
  void RegisterModule(llvm::StringRef MangledName,
                      schir::ModuleLoadNamesFn*);
};

}

#endif
