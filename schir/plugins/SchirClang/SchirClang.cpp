// Copyright Jason Rice 2025

#include <schir/Builtins.h>
#include <schir/Clang.h>
#include <schir/Context.h>
#include <schir/SchirScheme.h>
#include <schir/Value.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Pragma.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <functional>
#include <utility>

#include "LexerWriter.h"
#include "ClangUtil.h"
#include "TemplateProbe.h"

schir::ContextLocal SCHIR_CLANG_VAR(diag_error);
schir::ContextLocal SCHIR_CLANG_VAR(diag_warning);
schir::ContextLocal SCHIR_CLANG_VAR(diag_note);
schir::ContextLocal SCHIR_CLANG_VAR(hello_world);
schir::ContextLocal SCHIR_CLANG_VAR(write_lexer);
schir::ContextLocal SCHIR_CLANG_VAR(lexer_writer);
schir::ContextLocal SCHIR_CLANG_VAR(expr_eval);
schir::ContextLocal SCHIR_CLANG_VAR(expr_type);
schir::ContextLocal SCHIR_CLANG_VAR(template_probe);
schir::ContextLocal SCHIR_CLANG_VAR(flush_tokens);

namespace {
using schir_clang::DiagReport;
using schir_clang::LexerWriter;
using schir_clang::ParseExpression;
using schir_clang::RunTemplateProbe;
using schir_clang::getSourceLocation;

// The stuff we need to stay alive.
struct InstanceTy {
  clang::Parser& ClangParser;
  schir::SchirScheme SchirScheme;
  llvm::BumpPtrAllocator LexerSpellings; // TODO use PP scratch buffer
  schir_clang::LexerWriter LexerWriter;
  bool IsResuming = false;
  std::function<void(schir::SourceLocation Loc, llvm::StringRef Str)>
  LexerWriterFn;

  InstanceTy(clang::Parser& P)
    : ClangParser(P),
      SchirScheme(),
      LexerSpellings(),
      LexerWriter(P, LexerSpellings)
  { }
  InstanceTy(InstanceTy const&) = delete;
};

static std::unique_ptr<InstanceTy> Instance;

using Foo = clang::ParserPragmaHandler;
class SchirSchemePragmaHandler : public clang::ParserPragmaHandler {

public:
  SchirSchemePragmaHandler()
    : ParserPragmaHandler("schir_scheme")
  { }

  void CreateInst(clang::Parser& P, std::unique_ptr<InstanceTy>& Inst) {
    // All of the references we capture here should be stable
    // for the lifetime of the compiler.
    Inst = std::make_unique<InstanceTy>(P);
    auto& [_, SchirScheme,
           LexerSpellings, TheLexerWriter,
           IsResuming, LexerWriterFn] = *Inst;
    schir::SchirScheme& HS = SchirScheme;

    auto ErrorHandler = [&](llvm::StringRef Err,
                            schir::FullSourceLocation EmbeddedLoc) {
      auto& Diags = P.getPreprocessor().getDiagnostics();
      DiagReport<clang::DiagnosticsEngine::Level::Error>{}(
            SchirScheme, EmbeddedLoc, Diags, Err);
    };
    SchirScheme.RegisterErrorHandler(ErrorHandler);

    // Load the static builtin module.
    auto diag_gen = [&](auto DiagReportFn) {
      return [&, DiagReportFn](schir::Context& C, schir::ValueRefs Args) {
        if (Args.size() < 1 || Args.size() > 2) {
          C.RaiseError("invalid arity to function", C.getCallee());
          return;
        }

        if (!schir::isa<schir::String, schir::Symbol>(Args[0])) {
          C.RaiseError("expecting string or identifier", C.getCallee());
          return;
        }

        llvm::StringRef Err = Args[0].getStringRef();
        schir::SourceLocation Loc;
        if (Args.size() > 1)
          Loc = Args[1].getSourceLocation();

        auto& Diags = P.getPreprocessor().getDiagnostics();
        DiagReportFn(HS, Loc, Diags, Err);
        C.Cont();
      };
    };

    auto diag_error = diag_gen(
        DiagReport<clang::DiagnosticsEngine::Level::Error>{});
    auto diag_warning = diag_gen(
        DiagReport<clang::DiagnosticsEngine::Level::Warning>{});
    auto diag_note = diag_gen(
        DiagReport<clang::DiagnosticsEngine::Level::Note>{});

    auto hello_world = [](schir::Context& C, schir::ValueRefs Args) {
      llvm::errs() << "hello world (from clang)\n";
      C.Cont();
    };

    auto expr_eval = [&](schir::Context& C, schir::ValueRefs Args) {
      schir::SourceLocation Loc;
      schir::Value Input;
      if (Args.size() == 2) {
        // Accept any value that may have a source location.
        Loc = Args[0].getSourceLocation();
        Input = Args[1];
      } else if (Args.size() == 1) {
        Input = Args[0];
      } else {
        return C.RaiseError("invalid arity");
      }
      if (!schir::isa<schir::String, schir::Symbol>(Input)) {
        C.RaiseError("expecting string or identifier", C.getCallee());
        return;
      }
      llvm::StringRef Source = Input.getStringRef();
      if (!Loc.isValid())
        Loc = Input.getSourceLocation();
      if (!Loc.isValid())
        Loc = C.getLoc();

      clang::ExprResult ExprResult = ParseExpression(P, HS, LexerSpellings,
                                                     Loc, Source);

      // Process the parsing result if any.
      if (ExprResult.isInvalid()) {
        return C.RaiseError("clang expression parsing failed");
      }
      clang::Expr* Expr = ExprResult.get();

      if (Expr->isValueDependent()) {
        return C.RaiseError("cannot evaluate dependent expression");
      }

      // ConstantExpr eval.
      clang::Expr::EvalResult EvalResult;
      if (!Expr->EvaluateAsRValue(EvalResult,
            P.getActions().getASTContext())) {
        // The evaluation failed.
        // TODO Have Clang emit the diagnostics (ie From EvalResult.Diag)
        C.RaiseError("clang expression evaluation failed");
        return;
      }

      // Convert EvalResult/APValue to Scheme value.
      schir::Value Result;
      using APValue = clang::APValue;
      switch (EvalResult.Val.getKind()) {
        case APValue::None:
        case APValue::Indeterminate: {
          // ... Or maybe we allow errors. (ie like SFINAE)
          Result = schir::Undefined();
          C.RaiseError("clang expression evaluation failed");
          return;
        }
        case APValue::Int: {
          llvm::APSInt Int = EvalResult.Val.getInt();
          if (Expr->getType()->isBooleanType()) {
            Result = schir::Bool(Int.getBoolValue());
          } else if (Int.isSignedIntN(32)) {
            Result = schir::Int(Int.getZExtValue());
          }
          break;
        }
        // TODO Support these value types.
        case APValue::Float:
        case APValue::FixedPoint:
        case APValue::ComplexInt:
        case APValue::ComplexFloat:
        case APValue::Vector:
        case APValue::Array:
        case APValue::Struct:

        // Will not support.
        case APValue::LValue:
        case APValue::Union:
        case APValue::MemberPointer:
        case APValue::AddrLabelDiff:
          // Do nothing.
        break;
      }

      if (Result) {
        C.Cont(Result);
      } else {
        C.RaiseError("unsupported result type");
      }
    };

    auto expr_type = [&](schir::Context& C, schir::ValueRefs Args) {
      if (Args.size() != 1)
        return C.RaiseError("invalid arity");

      schir::Value Arg = Args.front();
      llvm::StringRef ExprStr = Arg.getStringRef();
      if (ExprStr.empty())
        return C.RaiseError("expecting nonempty string-like", Arg);

      schir::SourceLocation Loc = Arg.getSourceLocation();
      if (!Loc.isValid())
        Loc = C.getLoc();

      clang::ExprResult ExprResult = ParseExpression(P, HS, LexerSpellings,
                                                     Loc, ExprStr);
      // Process the parsing result if any.
      if (ExprResult.isInvalid())
        return C.RaiseError("clang expression parsing failed");
      clang::Expr* Expr = ExprResult.get();

      if (Expr->isValueDependent())
        return C.RaiseError("expression has dependent type");

      clang::QualType QT = Expr->getType();
      if (QT.isNull())
        return C.RaiseError("clang expression type failed");

      std::string ResultStr = schir_clang::TypeToString(QT);
      schir::Value Result = C.CreateSymbol(ResultStr);
      C.Cont(Result);
    };

    // (template-probe loc "my::foo"
    //  """
    //    nbdl::match(std::declval<SomeType>(), [](auto const& arg) {
    //      my::foo<std::remove_cvref_t<decltype(arg)>>{};
    //    });
    //  """)
    auto template_probe = [&](schir::Context& C, schir::ValueRefs Args) {
      if (Args.size() != 3)
        return C.RaiseError("invalid arity");
      schir::SourceLocation Loc = Args[0].getSourceLocation();
      llvm::StringRef TemplateName = Args[1].getStringRef();
      llvm::StringRef Expr = Args[2].getStringRef();
      if (TemplateName.empty())
        return C.RaiseError("expecting non empty string-like", Args[1]);
      if (Expr.empty())
        return C.RaiseError("expecting non empty string-like", Args[2]);
      RunTemplateProbe(P, HS, LexerSpellings, C, Loc, TemplateName, Expr);
    };

    // This is a special system specific function so we can
    // use Clang's file search and source locations.
    auto ParseSourceFileFn = [&P, &HS](schir::Context& C,
                                    schir::SourceLocation Loc,
                                    schir::String* RequestedFilename) {
      clang::Preprocessor& PP = P.getPreprocessor();
      schir::FullSourceLocation
        FullLoc = HS.getFullSourceLocation(Loc);
      clang::SourceLocation ClangLoc = getSourceLocation(FullLoc);
      clang::OptionalFileEntryRef File = PP.LookupFile(
          ClangLoc, RequestedFilename->getView(),
          false, nullptr, nullptr, nullptr, nullptr, nullptr,
          nullptr, nullptr, nullptr);
      if (!File) {
        schir::String* ErrMsg = C.CreateString("error opening file: ",
                                            RequestedFilename->getStringRef());
        return C.RaiseError(ErrMsg, schir::Value(RequestedFilename));
      }
      // Determine if file is a system file... as if!
      clang::SrcMgr::CharacteristicKind FileChar =
        PP.getHeaderSearchInfo()
          .getFileDirFlavor(*File);
      clang::FileID FileId =
        PP.getSourceManager().createFileID(*File, ClangLoc, FileChar);
      clang::SourceLocation StartLoc =
        PP.getSourceManager().getLocForStartOfFile(FileId);
      std::optional<llvm::MemoryBufferRef> Buffer =
        PP.getSourceManager().getBufferOrNone(FileId, ClangLoc);
      if (!Buffer)
        return C.RaiseError("error opening file buffer",
                            schir::Value(RequestedFilename));
      // Is it over yet?
      HS.ParseSourceFile(StartLoc.getRawEncoding(),
                         File->getName(),
                         Buffer->getBufferStart(),
                         Buffer->getBufferEnd(),
                         Buffer->getBufferStart());
    };

    schir::Context& Context = SchirScheme.getContext();
    schir::builtins::InitParseSourceFile(Context, ParseSourceFileFn);
    SCHIR_CLANG_VAR(diag_error).set(Context,
                                    Context.CreateLambda(diag_error));
    SCHIR_CLANG_VAR(diag_warning).set(Context,
                                      Context.CreateLambda(diag_warning));
    SCHIR_CLANG_VAR(diag_note).set(Context,
                                   Context.CreateLambda(diag_note));
    SCHIR_CLANG_VAR(hello_world).set(Context,
                                     Context.CreateLambda(hello_world));
    SCHIR_CLANG_VAR(expr_eval).set(Context,
                                    Context.CreateLambda(expr_eval));
    SCHIR_CLANG_VAR(expr_type).set(Context,
                                    Context.CreateLambda(expr_type));
    SCHIR_CLANG_VAR(template_probe).set(Context,
                                    Context.CreateLambda(template_probe));
    SCHIR_CLANG_VAR(write_lexer).set(Context,
        Context.CreateLambda([&](schir::Context& C,
                                 schir::ValueRefs Args) mutable {
      schir::SourceLocation Loc;
      schir::Value Output;
      if (Args.size() == 0)
        return C.RaiseError("invalid arity");
      if (Args.size() > 1) {
        // Accept any value that may have a source location.
        Loc = Args[0].getSourceLocation();
        Args = Args.drop_front();
      }
      llvm::SmallString<128> BufStr;
      for (schir::Value Output : Args) {
        if (!schir::isa<schir::String, schir::Symbol>(Output))
          return C.RaiseError(C.CreateString(
                "invalid type: ",
                schir::getKindName(Output.getKind()),
                ", expecting string or identifier"
                ), Output);
        BufStr.append(Output.getStringRef());

        // Try to get a valid source location.
        if (!Loc.isValid()) Loc = Output.getSourceLocation();
        if (!Loc.isValid()) Loc = C.getLoc();
      }

      llvm::StringRef Result(BufStr);
      TheLexerWriter.Tokenize(getSourceLocation(
            SchirScheme.getFullSourceLocation(Loc)),
            Result);
      C.Cont();
    }));

    // Also provide a type erased LexerWriterFnRef which is
    // more suited to calling in c++.
    LexerWriterFn = [&](schir::SourceLocation Loc, llvm::StringRef Str) {
      TheLexerWriter.Tokenize(getSourceLocation(
            SchirScheme.getFullSourceLocation(Loc)), Str);
    };
    auto LWF = schir::LexerWriterFnRef(LexerWriterFn);
    SCHIR_CLANG_VAR(lexer_writer).set(Context, Context.CreateAny(LWF));

    // Create scheme proc to allow the user to
    // flush tokens and continue evaluation.
    // Note that we merely break where flush is called
    // after evaluation is complete.
    auto FlushTokens = [&](schir::Context& C, schir::ValueRefs) {
      if (TheLexerWriter.empty())
        return C.Cont();
      IsResuming = true;
      schir::FullSourceLocation HSLoc = HS.getFullSourceLocation(C.getLoc());
      clang::SourceLocation Loc = getSourceLocation(HSLoc);
      TheLexerWriter.PushResumeToken(Loc, this);
      HS.Break();
    };
    SCHIR_CLANG_VAR(flush_tokens)
      .set(Context, Context.CreateLambda(FlushTokens));
    SchirScheme.RegisterModule(SCHIR_CLANG_LIB_STR, SCHIR_CLANG_LOAD_MODULE);
  }

  void HandleParseExternalDeclaration(
                    clang::Parser& P,
                    clang::Token& Tok,
                    clang::DeclGroupRef&) override {
    // Only supporting one instance.
    if (!Instance)
      CreateInst(P, Instance);
    assert(&Instance->ClangParser == &P);
    auto& [_, SchirScheme,
           _, TheLexerWriter,
           IsResuming, _] = *Instance;
    clang::Preprocessor& PP = P.getPreprocessor();

    schir::Context& Context = SchirScheme.getContext();

    // Do the thing or the other thing.
    if (!IsResuming) {
      schir::Lexer SchemeLexer;
      auto LexerInitFn = [&](clang::SourceLocation Loc,
                             llvm::StringRef Name,
                             char const* BufferStart,
                             char const* BufferEnd,
                             char const* BufferPtr) {
        SchemeLexer = SchirScheme.createEmbeddedLexer(
                            Loc.getRawEncoding(), Name,
                            BufferStart, BufferEnd, BufferPtr);
      };
      PP.InitEmbeddedLexer(LexerInitFn);
      schir::TokenKind Terminator = schir::tok::r_brace;
      SchirScheme.ParseTopLevelCommands(SchemeLexer, Terminator);
      PP.FinishEmbeddedLexer(SchemeLexer.GetByteOffset());
      SchirScheme.ProcessPendingExprs(schir::builtins::eval);
    } else {
      IsResuming = false;
      SchirScheme.Resume();
    }

    TheLexerWriter.FlushTokens();
    // If its not eod there was some issue with the pragma syntax.
    if (P.getCurToken().is(clang::tok::eod))
      P.ConsumeToken();
  }
};

} // namespace

static clang::PragmaHandlerRegistry::Add<SchirSchemePragmaHandler>
PragmaHandler("schir_scheme", "embed compile-time scheme");
