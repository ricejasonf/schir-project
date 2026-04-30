//===-- schir_main.cpp - SchirScheme ------------ --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the schir scheme interpreter.
//
//===----------------------------------------------------------------------===//

#include <schir/Builtins.h>
#include <schir/Lexer.h>
#include <schir/Parser.h>
#include <schir/SchirScheme.h>
#include <schir/SourceManager.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/Process.h>
#include <string>
#include <system_error>

namespace cl = llvm::cl;

enum class ExecutionMode {
  none,
  repl,
  read,
  mlir,
};

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::init("-"));

static cl::opt<ExecutionMode> InputMode(
  "mode", cl::desc("mode of execution"),
  cl::values(
    cl::OptionEnumValue{"repl",
                        (int)ExecutionMode::repl,
                        "read eval and print loop (not interactive yet)"},
    cl::OptionEnumValue{"read",
                        (int)ExecutionMode::read,
                        "just read and print"},
    cl::OptionEnumValue{"mlir",
                        (int)ExecutionMode::mlir,
                        "verify and output mlir code to stderr"}),
  cl::init(ExecutionMode::none));

static cl::list<std::string> InputIncludePaths(
  "module-path", cl::desc("Specify a path used for file lookup."));

static cl::alias InputModulePathAlias(
  "I", cl::desc("Alias for -module-path"), cl::aliasopt(InputIncludePaths));

static cl::opt<std::string> InputExportModule(
  "export-module", cl::desc("Specify a library by its mangled name to export"
                            " as bytecode to the module path."),
  cl::init(""));

void ProcessTopLevelExpr(schir::Context& Context, schir::ValueRefs Values) {
  assert(Values.size() == 2 && "expecting 2 arguments");
  schir::Value Val = Values[0];
  schir::Value Env = Values[1];
  switch (InputMode.getValue()) {
  case ExecutionMode::repl:
    schir::eval(Context, Val, Env);
    return;
  case ExecutionMode::mlir:
    schir::compile(Context, Val, Env, schir::Undefined());
    return;
  case ExecutionMode::read:
    Val.dump();
    Context.Cont();
    return;
  default:
    schir::eval(Context, Val, Env);
  }
}

void SetIncludePaths(schir::SchirScheme& SchirScheme) {
  schir::Context& C = SchirScheme.getContext();
  llvm::SmallVector<schir::Value, 4> Paths;
  for (std::string const& Str : InputIncludePaths) {
    auto AbsPath = llvm::SmallString<128>(llvm::StringRef(Str));
    llvm::sys::fs::make_absolute(AbsPath);
    Paths.push_back(C.CreateString(AbsPath.str()));
  }
  schir::Value PathList = C.CreateList(Paths);
  SchirScheme.SetIncludePaths(PathList);
}

int main(int argc, char const** argv) {
#if 0
  // TODO Provide interactive looping.
  //      Also look at llvm::LineEditor.
  bool IsInteractive = llvm::sys::Process::StandardInIsUserInput();
#endif
  llvm::InitLLVM LLVM_(argc, argv);
  schir::SchirScheme SchirScheme;
  SchirScheme.InitSourceFileStorage();
  cl::ParseCommandLineOptions(argc, argv);

  SetIncludePaths(SchirScheme);

  // Create error handler.
  bool HasErrors = false;
  auto OnError = [&HasErrors](llvm::StringRef Err,
                              schir::FullSourceLocation const& SL) {
    HasErrors = true;
    if (SL.isValid()) {
      schir::SourceLineContext LineContext = SL.getLineContext();
      llvm::errs() << LineContext.FileName
                   << ':' << LineContext.LineNumber
                   << ':' << LineContext.Column << ": "
                   << "error: " << Err << '\n'
                   << LineContext.LineRange << '\n';
      // Display the caret pointing to the point of interest.
      for (unsigned i = 1; i < LineContext.Column; i++) {
        llvm::errs() << ' ';
      }
      llvm::errs() << "^\n";
    } else {
      llvm::errs() << "error: " << Err << "\n\n";
    }
  };

  // Run the top level expressions in the file.
  SchirScheme.ProcessTopLevelCommands(InputFilename,
                                      ProcessTopLevelExpr,
                                      OnError);

  if (InputMode.getValue() == ExecutionMode::mlir) {
    SchirScheme.getContext().verifyModule();
    SchirScheme.getContext().printModuleOp();
  }
  if (HasErrors) std::exit(1);
}
