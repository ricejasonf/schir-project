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


namespace schir {
using ErrorHandlerFnTy =
  llvm::function_ref<schir::SchirScheme::ErrorHandlerFn>;

// Keep a list of parsed top level
// exprs to eventually be processed.
static ContextLocal PendingTopLevelExprs;

// Provide an escape proc if the user breaks.
static ContextLocal ResumeProc;

// Store an ErrorHandler that can be refreshed
// on the current stack frame.
// If set, expect it to be
//  Any<ErrorHandlerFnTy>
static ContextLocal RefreshableErrorHandler;

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

#if 0
// TODO Not sure if this is necessary
bool SchirScheme::HasPendingExprs() {
  Context& C = getContext();
  schir::Value Exprs = PendingTopLevelExprs.get(C);
  schir::Value Proc = ResumeProc.get(C);
  return isa<Pair>(ExprStack) || isa<Lambda>(Proc);
}
#endif

void SchirScheme::Break() {
  Context& C = getContext();
  C.CallCC(C.CreateLambda([](Context& C, ValueRefs Args) {
    ResumeProc.set(C, Args[0]);
    C.Yield(Undefined());
  }));
}

bool SchirScheme::Resume(ErrorHandlerFnTy ErrorHandler) {
  Context& C = getContext();
  RefreshableErrorHandler.set(C, C.CreateAny<ErrorHandlerFnTy>(ErrorHandler));
  Lambda* Proc = dyn_cast<Lambda>(ResumeProc.get(C));
  ResumeProc.set(C, Undefined());
  Value Undef = Undefined();
  if (Proc)
    C.RunSync(Proc, Undef);
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

// Note this captures a the function_ref which
// must be kept alive in the parent stack frame.
static auto
CreateErrorHandler(Context& C, ErrorHandlerFnTy ErrorHandler) {
  // Capture the function_ref in a global to be accessible in scheme land
  // and refreshable in subsequent calls to Resume.
  using FnTy = ErrorHandlerFnTy;
  RefreshableErrorHandler.set(C, C.CreateAny<FnTy>(ErrorHandler));
  return [](schir::Context& C, ValueRefs Args = {}) {
    FnTy ErrorHandler = any_cast<FnTy>(RefreshableErrorHandler.get(C));
    if (!ErrorHandler)
      ErrorHandler = [](auto&& ...) {
        /* Do nothing by default. */
      };
    auto* SM = C.SourceManager;
    schir::FullSourceLocation FullLoc = SM->getFullSourceLocation(
        Args[0].getSourceLocation());
    if (schir::Error* Err = dyn_cast<schir::Error>(Args[0])) {
      String* FmtMessage = C.CreateFormatted(Err);
      ErrorHandler(FmtMessage->getStringRef(), FullLoc);
    } else {
      ErrorHandler("errorhandler received a non-error", FullLoc);
    }
    C.Cont();
  };
}

void SchirScheme::ParseTopLevelCommands(
                              schir::Lexer& Lexer,
                              ErrorHandlerFnTy ErrorHandler,
                              schir::tok Terminator) {
  auto& Context = getContext();
  auto& SM = getSourceManager();
  Context.SourceManager = &SM;
  auto ParserPtr = std::make_unique<Parser>(Lexer, Context);
  Parser& Parser = *ParserPtr;
  auto HandleErrorFn = CreateErrorHandler(Context, ErrorHandler);

  if (!Parser.PrimeToken(Terminator)) {
    HandleErrorFn(Context, {});
    return;
  }

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
    [](schir::Context& Context, ValueRefs) {
      Value HandleError = Context.getCapture(0);
      Value MainThunk = Context.getCapture(1);
      Context.WithExceptionHandler(HandleError, MainThunk);
    }, CaptureList{HandleError, MainThunk}));

  // Run the loop.
  Context.Resume();
}

void SchirScheme::ProcessTopLevelCommands(
                              schir::Lexer& Lexer,
                              llvm::function_ref<ValueFnTy> ExprHandler,
                              llvm::function_ref<ErrorHandlerFn> ErrorHandler,
                              schir::tok Terminator) {
  ParseTopLevelCommands(Lexer, ErrorHandler, Terminator);
  ProcessPendingExprs(ExprHandler, ErrorHandler);
}

// ParseTopLevelCommands pushed to PendingTopLevelExprs
// so now we apply them to ExprHandler.
void SchirScheme::ProcessPendingExprs(
                            llvm::function_ref<ValueFnTy> ExprHandler,
                            llvm::function_ref<ErrorHandlerFn> ErrorHandler) {
  auto& Context = getContext();
  auto& SM = getSourceManager();
  Context.SourceManager = &SM;
  schir::Environment* Env = Context.DefaultEnv.get();

  Value HandleExpr = Context.CreateLambda(ExprHandler, {});
  Value HandleError = Context.CreateLambda(
                          CreateErrorHandler(Context, ErrorHandler), {});
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
