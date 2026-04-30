//===--- SourceFileStorage.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// System touching implementations for the ParseSourceFile hook in SchirScheme
// and others.
//
//===----------------------------------------------------------------------===//

#include "schir/Builtins.h"
#include "schir/SchirScheme.h"
#include "schir/SourceFileStorage.h"
#include "schir/SourceManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace schir {

// SchirScheme

void SchirScheme::InitSourceFileStorage() {
  using PtrTy = decltype(SourceFileStoragePtr);
  SourceFileStoragePtr = PtrTy(new SourceFileStorage(),
                    [](SourceFileStorage* S) { delete S; });
  schir::builtins::InitParseSourceFile(getContext(), 
                                   [this](schir::Context& C,
                                          schir::SourceLocation Loc,
                                          schir::String* Filename) {
    schir::Value Result = this->ParseSourceFile(Loc, Filename->getView());
    // The parser already raised an error.
    if (llvm::isa<schir::Undefined>(Result))
      return;
    C.Cont(Result);
  });
}

void SchirScheme::ProcessTopLevelCommands(llvm::StringRef Filename,
                          llvm::function_ref<ValueFnTy> ExprHandler,
                          llvm::function_ref<ErrorHandlerFn> ErrorHandler) {
  schir::SourceManager& SM = getSourceManager();
  if (!SourceFileStoragePtr) {
    ErrorHandler("source file storage not initialized",
                 SM.getFullSourceLocation({}));
    return;
  }
  std::string ErrorMessage;
  llvm::ErrorOr<schir::SourceFile>
    FileResult = SourceFileStoragePtr->Open(SM, schir::SourceLocation(),
                                            Filename, ErrorMessage);
  if (!FileResult) {
    ErrorHandler(ErrorMessage, SM.getFullSourceLocation({}));
    return;
  }
  schir::Lexer Lexer(FileResult.get());
  ProcessTopLevelCommands(Lexer, ExprHandler, ErrorHandler, tok::eof);
}

// Indicate an error was raised by returning an empty string.
static std::string SearchIncludePath(schir::Context& C,
                                     llvm::StringRef Filename) {
  schir::Value IncludePaths = SCHIR_BASE_VAR(include_paths).get(C);
  for (schir::Value Path : IncludePaths) {
    llvm::StringRef PathStr = Path.getStringRef();
    if (PathStr.empty()) {
      C.RaiseError(
          "expecting nonempty string-like object in include-paths: {}", Path);
      return std::string();
    }
    llvm::Twine T2(Filename);
    llvm::Twine T1(PathStr, "/");
    if (llvm::sys::fs::exists(T1.concat(T2)))
      return T1.concat(T2).str();
  }

  C.RaiseError("file not found using include-paths: {}",
      Value(C.CreateString(Filename)));
  return std::string();
}

// This overload provides the default file system
// access for opening source files.
// The user must call InitSourceFileStorage.
schir::Value SchirScheme::ParseSourceFile(schir::SourceLocation Loc,
                                          llvm::StringRef Filename) {
  assert(SourceFileStoragePtr &&
      "source file storage not initialized");

  std::string FullPath = SearchIncludePath(getContext(), Filename);
  if (FullPath.empty())
    return Undefined();

  std::string ErrorMessage;
  llvm::ErrorOr<schir::SourceFile>
    FileResult = SourceFileStoragePtr->Open(getSourceManager(), Loc,
                                            FullPath, ErrorMessage);
  if (!FileResult) {
    schir::Context& C = getContext();
    C.RaiseError(ErrorMessage);
    return Undefined();
  }
  schir::Lexer Lexer(FileResult.get());
  return ParseSourceFile(Lexer);
}

llvm::ErrorOr<schir::SourceFile>
SourceFileStorage::Open(schir::SourceManager& SM,
                        schir::SourceLocation IncludeLoc,
                        llvm::StringRef Filename,
                        std::string& ErrorMessage) {
  llvm::SmallString<128> FilePath;
  // Module files already have an absolute path.
  if (!Filename.ends_with(".sld")) {
    // Include Loc may be invalid.
    schir::SourceFile IncludeSF = SM.getFile(IncludeLoc);
    if (IncludeSF.isValid() && IncludeSF.Name != "-") {
      // Get the file path.
      FilePath = IncludeSF.Name;
      llvm::sys::path::remove_filename(FilePath);
    }
  }

  FilePath += Filename;

  // Check LookupCache
  schir::SourceLocation CachedLoc = LookupCache.lookup(FilePath); 
  if (CachedLoc.isValid()) {
    schir::SourceFile CachedSF = SM.getFile(CachedLoc);
    assert(CachedSF.isValid() && "cached source file should be valid");
    return CachedSF;
  }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
    llvm::MemoryBuffer::getFileOrSTDIN(FilePath);
  if (std::error_code ec = File.getError()) {
    ErrorMessage = (llvm::Twine("opening ", FilePath) + 
                             llvm::Twine(": ", ec.message())).str();
    return File.getError();
  }

  // Ensure the name is stable in memory.
  NodeTy& Node = Storage.emplace_back(std::move(*File));
  return SM.createEntry(Node->getBuffer(), FilePath);
}

}
