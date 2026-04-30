//===--- SchirScheme.cpp - SchirScheme Context Implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations for the opaque interfaces in SchirScheme.
//
//===----------------------------------------------------------------------===//

#include "schir/Builtins.h"
#include "schir/Context.h"
#include "schir/SchirScheme.h"
#include "schir/Lexer.h"
#include "schir/Mangle.h"
#include "schir/OpGen.h"
#include "schir/Parser.h"
#include "schir/Source.h"
#include "schir/SourceManager.h"
#include "schir/Value.h"


namespace schir {

SchirScheme::SchirScheme(std::unique_ptr<schir::Context> C)
  : ContextPtr(std::move(C)),
    SourceManagerPtr(std::make_unique<schir::SourceManager>()),
    SourceFileStoragePtr(nullptr, [](SourceFileStorage*) { })
{
  // Create and store the default environment.
  ContextPtr->DefaultEnv = std::make_unique<schir::Environment>(*ContextPtr);
}

SchirScheme::SchirScheme()
  : SchirScheme(std::make_unique<schir::Context>())
{ }

SchirScheme::~SchirScheme() = default;

// Create a Lexer using a "file" from some externally managed source.
schir::Lexer SchirScheme::createEmbeddedLexer(uintptr_t ExternalRawLoc,
                                              llvm::StringRef Name,
                                              char const* BufferStart,
                                              char const* BufferEnd,
                                              char const* BufferPos) {
  schir::SourceManager& SM = getSourceManager();
  size_t BufferLen = BufferEnd - BufferStart;
  llvm::StringRef FileBuffer(BufferStart, BufferLen);
  schir::SourceFile File = SM.getOrCreateExternal(ExternalRawLoc, FileBuffer,
                                                  Name);
  return Lexer(File, BufferPos);
}

void SchirScheme::ProcessTopLevelCommands(
                              schir::Lexer& Lexer,
                              llvm::function_ref<ValueFnTy> ExprHandler,
                              llvm::function_ref<ErrorHandlerFn> ErrorHandler,
                              schir::tok Terminator) {
  auto& Context = getContext();
  auto& SM = getSourceManager();
  Context.SourceManager = &SM;
  auto ParserPtr = std::make_unique<Parser>(Lexer, Context);
  Parser& Parser = *ParserPtr;
  schir::Environment* Env = Context.DefaultEnv.get();

  auto HandleErrorFn = [&](schir::Context& Context, ValueRefs Args) {
    schir::FullSourceLocation FullLoc = SM.getFullSourceLocation(
        Args[0].getSourceLocation());
    if (schir::Error* Err = dyn_cast<schir::Error>(Args[0])) {
      String* FmtMessage = Context.CreateFormatted(Err);
      ErrorHandler(FmtMessage->getStringRef(), FullLoc);
    } else {
      ErrorHandler("errorhandler received a non-error", FullLoc);
    }
    // Finish parsing to return control to any
    // parent lexer. (ie So c++ does not parse scheme code)
    while (!Parser.isFinished()) {
      Parser.ParseTopLevelExpr();
    }
    Context.Cont();
  };

  if (!Parser.PrimeToken(Terminator)) {
    HandleErrorFn(Context, {});
    return;
  }

  Value HandleExpr = Context.CreateLambda(ExprHandler, {});
  Value HandleError = Context.CreateLambda(HandleErrorFn, {});
  Value MainThunk = Context.CreateLambda([&Parser]
                          (schir::Context& C, ValueRefs) {
    if (Parser.isFinished()) {
      C.Cont();
      return;
    }

    // Recurse in tail position.
    C.PushCont(C.getCallee());

    schir::ValueResult ParseResult = Parser.ParseTopLevelExpr();
    if (ParseResult.isUsable()) {
      Value Env = C.getCapture(0);
      Value HandleExpr = C.getCapture(1);
      Value ParseResultVal = ParseResult.get();
      C.setLoc(ParseResultVal.getSourceLocation());
      std::array<Value, 2> EvalArgs{ParseResultVal, Env};
      C.Apply(HandleExpr, EvalArgs);
    } else if (Parser.HasError()) {
      Parser.RaiseError();
    } else {
      C.Cont();
    }
  }, CaptureList{Value(Env), HandleExpr});

  Context.DynamicWind(std::move(ParserPtr), Context.CreateLambda(
    [](schir::Context& Context, ValueRefs) {
      Value HandleError = Context.getCapture(0);
      Value MainThunk = Context.getCapture(1);
      Context.WithExceptionHandler(HandleError, MainThunk);
    }, CaptureList{HandleError, MainThunk}));

  // Run the loop.
  Context.Resume();
}

void SchirScheme::RegisterModule(llvm::StringRef MangledName,
                                 schir::ModuleLoadNamesFn* LoadNamesFn) {
  getContext().RegisterModule(MangledName, LoadNamesFn);
}

schir::FullSourceLocation
SchirScheme::getFullSourceLocation(schir::SourceLocation Loc) {
  return getSourceManager().getFullSourceLocation(Loc);
}

schir::Value SchirScheme::ParseSourceFile(uintptr_t ExternalRawLoc,
                                          llvm::StringRef Name,
                                          char const* BufferStart,
                                          char const* BufferEnd,
                                          char const* BufferPos) {
  schir::Lexer Lexer = createEmbeddedLexer(ExternalRawLoc, Name,
                                           BufferStart, BufferEnd,
                                           BufferPos);
  return ParseSourceFile(Lexer);
}

schir::Value SchirScheme::ParseSourceFile(schir::Lexer Lexer) {
  schir::Parser Parser(Lexer, getContext());
  schir::ValueResult Result = Parser.Parse();
  if (Parser.HasError()) {
    Parser.RaiseError();
    return Undefined();
  }
  return Result.get();
}

// IncludePaths should be a proper or improper list of strings.
void SchirScheme::SetIncludePaths(schir::Value IncludePaths) {
  schir::Context& C = getContext();
  SCHIR_BASE_VAR(include_paths).set(C, IncludePaths);
}

}
