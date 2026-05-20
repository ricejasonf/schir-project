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

#include <schir/Builtins.h>
#include <schir/Context.h>
#include <schir/SchirScheme.h>
#include <schir/Lexer.h>
#include <schir/Mangle.h>
#include <schir/OpGen.h>
#include <schir/Parser.h>
#include <schir/Source.h>
#include <schir/SourceManager.h>
#include <schir/Value.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <utility>


namespace schir {
// Keep a list of parsed top level
// exprs to eventually be processed.
static ContextLocal PendingTopLevelExprs;

// Provide an escape proc if the user breaks.
static ContextLocal ResumeProc;

// Store an ErrorHandler that can be refreshed
// on the current stack frame.
// If set, expect it to be
//  Any<ErrorHandlerFnTy>
static ContextLocal ErrorHandler;

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

void SchirScheme::Break() {
  Context& C = getContext();
  C.CallCC(C.CreateLambda([](Context& C, ValueRefs Args) {
    ResumeProc.set(C, Args[0]);
    C.Yield(Undefined());
  }));
}

bool SchirScheme::Resume() {
  Context& C = getContext();
  Lambda* Proc = dyn_cast<Lambda>(ResumeProc.get(C));
  ResumeProc.set(C, Undefined());
  Value Undef = Undefined();
  if (Proc) {
    C.Apply(Proc, Undef);
    C.Resume();
  }
  return Proc != nullptr;
}

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

void SchirScheme::RegisterErrorHandler(std::function<UserErrorHandlerFn> Fn) {
  Context& C = getContext();
  UserErrorHandler = std::move(Fn);
  Lambda* L = C.CreateLambda([this](Context& C, ValueRefs Args) mutable {
    auto* SM = C.SourceManager;
    schir::FullSourceLocation FullLoc = SM->getFullSourceLocation(
        Args[0].getSourceLocation());
    if (schir::Error* Err = dyn_cast<schir::Error>(Args[0])) {
      String* FmtMessage = C.CreateFormatted(Err);
      UserErrorHandler(FmtMessage->getStringRef(), FullLoc);
    } else {
      UserErrorHandler("errorhandler received a non-error", FullLoc);
    }
    C.Cont();
  });
  RegisterErrorHandler(L);
}

void SchirScheme::RegisterErrorHandler(Lambda* Fn) {
  ErrorHandler.set(getContext(), Fn);
}

void SchirScheme::ParseTopLevelCommands(
                              schir::Lexer& Lexer,
                              schir::tok Terminator) {
  auto& Context = getContext();
  auto& SM = getSourceManager();
  Context.SourceManager = &SM;
  auto ParserPtr = std::make_unique<Parser>(Lexer, Context);
  Parser& Parser = *ParserPtr;
  Parser.PrimeToken(Terminator);

  Value MainThunk = Context.CreateLambda([&Parser]
                          (schir::Context& C, ValueRefs) {
    if (Parser.isFinished()) {
      C.Cont();
      return;
    }

    // Recurse in tail position.
    C.PushCont(C.getCallee());

    schir::ValueResult ParseResult = Parser.ParseTopLevelExpr();
    if (Parser.HasError()) {
      Parser.RaiseError();
        // Finish parsing to return control to any
        // parent lexer. (ie So c++ does not parse scheme code)
        while (!Parser.isFinished())
          Parser.ParseTopLevelExpr();
    } else {
      if (ParseResult.isUsable()) {
        // Push to PendingTopLevelExprs..
        Value Prev = PendingTopLevelExprs.get(C);
        if (!isa<Empty, Pair>(Prev))  // Could be Undefined.
          Prev = Empty();
        C.PushCont([](schir::Context& C, ValueRefs Args) {
          PendingTopLevelExprs.set(C, Args[0]);
          C.Cont();
        });
        C.Apply(schir::builtins_var::append,
                {Prev, C.CreatePair(ParseResult.get())});
        return;
      }
      C.Cont();
    }
  }, CaptureList{});

  Context.DynamicWind(std::move(ParserPtr), Context.CreateLambda(
    [](schir::Context& C, ValueRefs) {
      Value HandleError = ErrorHandler.get(C);
      Value MainThunk = C.getCapture(0);
      C.WithExceptionHandler(HandleError, MainThunk);
    }, CaptureList{MainThunk}));

  // Run the loop.
  Context.Resume();
}

void SchirScheme::ProcessTopLevelCommands(
                              schir::Lexer& Lexer,
                              llvm::function_ref<ValueFnTy> ExprHandler,
                              schir::tok Terminator) {
  ParseTopLevelCommands(Lexer, Terminator);
  ProcessPendingExprs(ExprHandler);
}

// ParseTopLevelCommands pushed to PendingTopLevelExprs
// so now we apply them to ExprHandler.
void SchirScheme::ProcessPendingExprs(
                            llvm::function_ref<ValueFnTy> ExprHandler) {
  auto& Context = getContext();
  auto& SM = getSourceManager();
  Context.SourceManager = &SM;
  schir::Environment* Env = Context.DefaultEnv.get();

  Value HandleExpr = Context.CreateLambda(ExprHandler, {});
  Value MainThunk = Context.CreateLambda([](schir::Context& C, ValueRefs) {
    Value TLExprs = PendingTopLevelExprs.get(C);
    Pair* P = dyn_cast<Pair>(TLExprs);
    if (!P)
      return C.Cont();  // Finished

    // Recurse in tail position.
    C.PushCont(C.getCallee());

    PendingTopLevelExprs.set(C, P->Cdr);
    Value Expr = P->Car;
    C.setLoc(Expr.getSourceLocation());
    Value Env = C.getCapture(0);
    Value HandleExpr = C.getCapture(1);
    C.Apply(HandleExpr, {Expr, Env});
  }, CaptureList{Value(Env), HandleExpr});

  Value HandleError = ErrorHandler.get(Context);
  Context.WithExceptionHandler(HandleError, MainThunk);
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

void SchirScheme::ParseSourceFile(uintptr_t ExternalRawLoc,
                                  llvm::StringRef Name,
                                  char const* BufferStart,
                                  char const* BufferEnd,
                                  char const* BufferPos) {
  schir::Lexer Lexer = createEmbeddedLexer(ExternalRawLoc, Name,
                                           BufferStart, BufferEnd,
                                           BufferPos);
  ParseSourceFile(Lexer);
}

void SchirScheme::ParseSourceFile(schir::Lexer Lexer) {
  schir::Context& C = getContext();
  schir::Parser Parser(Lexer, C);
  schir::ValueResult Result = Parser.Parse();
  if (Parser.HasError())
    Parser.RaiseError();
  else
    C.Cont(Result.get());
}

// IncludePaths should be a proper or improper list of strings.
void SchirScheme::SetIncludePaths(schir::Value IncludePaths) {
  schir::Context& C = getContext();
  SCHIR_BASE_VAR(include_paths).set(C, IncludePaths);
}

}
