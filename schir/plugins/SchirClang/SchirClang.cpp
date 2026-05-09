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
#include <utility>

schir::ContextLocal SCHIR_CLANG_VAR(diag_error);
schir::ContextLocal SCHIR_CLANG_VAR(diag_warning);
schir::ContextLocal SCHIR_CLANG_VAR(diag_note);
schir::ContextLocal SCHIR_CLANG_VAR(hello_world);
schir::ContextLocal SCHIR_CLANG_VAR(write_lexer);
schir::ContextLocal SCHIR_CLANG_VAR(lexer_writer);
schir::ContextLocal SCHIR_CLANG_VAR(expr_eval);
schir::ContextLocal SCHIR_CLANG_VAR(template_probe);

namespace {
using Pair = std::pair<clang::Parser*, std::unique_ptr<schir::SchirScheme>>;

static auto Instance = Pair();

// Convert to a clang::SourceLocation or an invalid location if it
// is not external.
clang::SourceLocation getSourceLocation(schir::FullSourceLocation Loc) {
  if (!Loc.isExternal()) return clang::SourceLocation();
  return clang::SourceLocation
    ::getFromRawEncoding(Loc.getExternalRawEncoding())
     .getLocWithOffset(Loc.getOffset());
}

template <clang::DiagnosticsEngine::Level Level>
struct DiagReport {
  void operator()(schir::SchirScheme& HS,
                  schir::SourceLocation Loc,
                  clang::DiagnosticsEngine& Diags,
                  llvm::StringRef ErrMsg) const {
    schir::FullSourceLocation FullLoc = HS.getFullSourceLocation(Loc);
    this->operator()(HS, FullLoc, Diags, ErrMsg);
  }

  void operator()(schir::SchirScheme& HS,
                  schir::FullSourceLocation HSLoc,
                  clang::DiagnosticsEngine& Diags,
                  llvm::StringRef ErrMsg) const {
    // Create a custom DiagId once for our instance.
    static schir::ContextLocal CustomDiagId;
    schir::Context& Context = HS.getContext();
    schir::Binding* DiagIdBinding = CustomDiagId.getBinding(Context);
    if (schir::isa<schir::Undefined>(DiagIdBinding->getValue())) {
      unsigned Id = Diags.getCustomDiagID(Level, "(schir_scheme) %0");
      DiagIdBinding->setValue(schir::Int(static_cast<int32_t>(Id)));
    }
    unsigned DiagId = static_cast<unsigned>(
        schir::cast<schir::Int>(DiagIdBinding->getValue()));
    clang::SourceLocation Loc = getSourceLocation(HSLoc);
    Diags.Report(Loc, DiagId) << ErrMsg;
  }
};

// It is complicated to keep the TokenBuffer alive
// for the Preprocessor, so we use an array to give
// ownership via the EnterTokenStream overload.
class LexerWriter {
  using Token = clang::Token;
  clang::Parser& Parser;
  llvm::BumpPtrAllocator& LexerSpellings;
  std::unique_ptr<Token[]> TokenBuffer;
  unsigned Capacity = 0;
  unsigned Size = 0;

  void realloc(unsigned NewCapacity) {
    std::unique_ptr<Token[]> NewTokenBuffer(new Token[NewCapacity]());
    if (Capacity > 0)
      std::copy(&TokenBuffer[0], &TokenBuffer[Size],
                NewTokenBuffer.get());
    TokenBuffer = std::move(NewTokenBuffer);
    Capacity = NewCapacity;
  }

  void push_back(Token Tok) {
    unsigned NewSize = Size + 1;
    if (Capacity < NewSize) {
      // Start with a reasonable 128 bytes and then
      // double capacity each time it is needed.
      unsigned NewCapacity = Capacity > 0 ? Capacity * 2 : 128;
      realloc(NewCapacity);
    }
    TokenBuffer[Size] = Tok;
    Size = NewSize;
  }

public:
  LexerWriter(clang::Parser& P,
              llvm::BumpPtrAllocator& LexerSpellings)
    : Parser(P),
      LexerSpellings(LexerSpellings),
      TokenBuffer(nullptr)
  { }

  ~LexerWriter() {
    Capacity = 0;
    Size = 0;
  }

  // Lex tokens from string and push to TokenBuffer.
  // Copy to a std::string to guarantee a null terminator.
  void Tokenize(clang::SourceLocation Loc, llvm::StringRef Chars) {
    if (Chars.empty()) return;
    // Copy to LexerSpellings to ensure null terminator.
    Chars = Chars.copy(LexerSpellings);
    char* NullTerm = LexerSpellings.template Allocate<char>(1);
    *NullTerm = 0;
    // Lex Tokens for the TokenBuffer.
    clang::Lexer Lexer(clang::SourceLocation(), Parser.getLangOpts(),
            Chars.data(), Chars.data(), &(*(Chars.end())));
    while (true) {
      Token Tok;
      Lexer.LexFromRawLexer(Tok);

      // Raw identifiers need to be looked up.
      if (Tok.is(clang::tok::raw_identifier))
        Parser.getPreprocessor().LookUpIdentifierInfo(Tok);

      Tok.setLocation(Loc);

      if (Tok.is(clang::tok::eof)) break;
      push_back(Tok);
    }
  }

#if 0 // TODO REMOVE if not used
  // Clear without flushing.
  void ClearTokens() {
    TokenBuffer.reset();
    Capacity = 0;
    Size = 0;
  }
#endif

  // This must be called AFTER we update the Clang Lexer position.
  void FlushTokens() {
    if (Size == 0) return;
    Parser.getPreprocessor().EnterTokenStream(std::move(TokenBuffer), Size,
                    /*DisableMacroExpansion=*/true,
                    /*IsReinject=*/true);
    Capacity = 0;
    Size = 0;
  }
};

template <typename Fn>
auto ParseSource(clang::Parser& P, schir::SchirScheme& HS,
                 schir::SourceLocation Loc,
                 llvm::StringRef Source,
                 Fn&& Thunk) {
  // Prepare to revert Parser. This is needed when there is a parse
  // error in eval_expr and it will clean up by parsing to the next
  // semicolon or whatever unless we are in tentative parsing mode.
  clang::Parser::RevertingTentativeParsingAction ParseReverter(P);

  // Lex and expand.
  LexerWriter TheLexerWriter(P, *HS.LexerSpellings);
  TheLexerWriter.Tokenize(getSourceLocation(HS.getFullSourceLocation(Loc)),
                          Source);
  TheLexerWriter.FlushTokens();
  P.ConsumeAnyToken();

  return Thunk();
}

clang::ExprResult ParseExpression(clang::Parser& P, schir::SchirScheme& HS,
                                  schir::SourceLocation Loc,
                                  llvm::StringRef Source) {
  return ParseSource(P, HS, Loc, Source, [&] {
    // Parse the expression.
    return P.ParseExpression();
  });
}

// Upon evaluating FullExpr, create a scheme linked list of lists
// of the names of argument types deduced by instantiating the
// call operator of a function object determined by Typename.
// TODO Determine if this will possibly include previous instantiations.
void RunTemplateProbe(clang::Parser& P, schir::SchirScheme& HS,
                      schir::Context& C,
                      schir::SourceLocation Loc,
                      llvm::StringRef TemplateName,
                      llvm::StringRef Expr) {
  clang::SourceLocation CLoc = getSourceLocation(HS.getFullSourceLocation(Loc));
  clang::TemplateDecl*
  TemplateDecl = ParseSource(P, HS, Loc, TemplateName,
    [&] -> clang::TemplateDecl* {
      clang::Sema& S = P.getActions();
      clang::CXXScopeSpec SS;
      clang::UnqualifiedId UnqualifiedId;
      P.ParseOptionalCXXScopeSpecifier(SS,
          /*ObjectType=*/nullptr, /*ObjectHasErrors=*/false,
          /*EnteringContext=*/false, /*IsAddressOf=*/false);
      P.ParseUnqualifiedId(SS,
          /*ObjectType=*/nullptr, /*ObjectHasErrors=*/false,
          /*EnteringContext=*/false,
          /*AllowConstructorName=*/false,
          /*AllowDesctuctorName=*/false,
          /*AllowDeductionGuide=*/false,
          /*TemplateKWLoc=*/nullptr,
          UnqualifiedId);

      if (UnqualifiedId.getKind() != clang::UnqualifiedIdKind::IK_Identifier)
        return nullptr;
      clang::DeclarationName DeclName(UnqualifiedId.Identifier);
      clang::LookupResult LR(S, DeclName, CLoc,
                             clang::Sema::LookupOrdinaryName);
      S.LookupParsedName(LR, S.getCurScope(), &SS, clang::QualType());
      return LR.getAsSingle<clang::TemplateDecl>();
    });

  if (!TemplateDecl)
    return C.RaiseError("expecting template name for probe");

  TemplateDecl->dump();

  //clang::ExprResult TNResult = P.ParseTemplateTemplateArgument();
  //TNResult.get()->dump();

  // TODO
  // Prepare to record all the instantiations of Template
  // via template inst callback or whatever.

  //ParseExpression(P, HS, Loc, Expr);

  // TODO Remove template inst callback or whatever.
}

using Foo = clang::ParserPragmaHandler;
class SchirSchemePragmaHandler : public clang::ParserPragmaHandler {
public:
  SchirSchemePragmaHandler()
    : ParserPragmaHandler("schir_scheme")
  { }

  void CreateInst(clang::Parser& P, Pair& Inst) {
    auto& [ParserPtr, SchirScheme] = Inst;
    ParserPtr = &P;
    SchirScheme = std::make_unique<schir::SchirScheme>();
    SchirScheme->LexerSpellings = std::make_unique<llvm::BumpPtrAllocator>();
    // Load the static builtin module.
    schir::SchirScheme& HS = *SchirScheme;
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

      clang::ExprResult ExprResult = ParseExpression(P, HS, Loc, Source);

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
          EvalResult.Val.dump();
        break;
      }

      if (Result) {
        C.Cont(Result);
      } else {
        C.RaiseError("unsupported result type");
      }
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
        C.RaiseError("expecting non empty string-like", Args[1]);
      if (Expr.empty())
        C.RaiseError("expecting non empty string-like", Args[2]);
      RunTemplateProbe(P, HS, C, Loc, TemplateName, Expr);
      // TODO continue with a list of the type names.
      C.Cont();
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
      C.Cont(HS.ParseSourceFile(StartLoc.getRawEncoding(),
                                File->getName(),
                                Buffer->getBufferStart(),
                                Buffer->getBufferEnd(),
                                Buffer->getBufferStart()));
    };

    schir::Context& Context = SchirScheme->getContext();
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
    SCHIR_CLANG_VAR(template_probe).set(Context,
                                    Context.CreateLambda(template_probe));
    SchirScheme->RegisterModule(SCHIR_CLANG_LIB_STR, SCHIR_CLANG_LOAD_MODULE);
  }

  void HandleParseExternalDeclaration(
                    clang::Parser& P,
                    clang::Token& Tok,
                    clang::DeclGroupRef&) override {
    auto& [ParserPtr, SchirScheme] = Instance;
    // Only supporting one instance.
    if (ParserPtr == nullptr)
      CreateInst(P, Instance);
    else if (&P != ParserPtr)
      return;

    schir::Lexer SchemeLexer;
    auto LexerInitFn = [&](clang::SourceLocation Loc,
                           llvm::StringRef Name,
                           char const* BufferStart,
                           char const* BufferEnd,
                           char const* BufferPtr) {
      SchemeLexer = SchirScheme->createEmbeddedLexer(
                          Loc.getRawEncoding(), Name,
                          BufferStart, BufferEnd, BufferPtr);
    };

    clang::Preprocessor& PP = P.getPreprocessor();
    PP.InitEmbeddedLexer(LexerInitFn);

    bool HasError = false;
    auto ErrorHandler = [&](llvm::StringRef Err,
                            schir::FullSourceLocation EmbeddedLoc) {
      HasError = true;
      auto& Diags = PP.getDiagnostics();
      DiagReport<clang::DiagnosticsEngine::Level::Error>{}(
            *SchirScheme, EmbeddedLoc, Diags, Err);
    };

    LexerWriter TheLexerWriter(P, *SchirScheme->LexerSpellings);
    schir::Context& Context = SchirScheme->getContext();
    SCHIR_CLANG_VAR(write_lexer).set(Context,
        Context.CreateLambda([&](schir::Context& C,
                                 schir::ValueRefs Args) mutable {
      schir::SourceLocation Loc;
      schir::Value Output;
      if (Args.size() == 2) {
        // Accept any value that may have a source location.
        Loc = Args[0].getSourceLocation();
        Output = Args[1];
      } else if (Args.size() == 1) {
        Output = Args[0];
      } else {
        return C.RaiseError("invalid arity");
      }
      if (!schir::isa<schir::String, schir::Symbol>(Output))
        return C.RaiseError(C.CreateString(
              "invalid type: ",
              schir::getKindName(Output.getKind()),
              ", expecting string or identifier"
              ), Output);

      // Try to get a valid source location.
      if (!Loc.isValid()) Loc = Output.getSourceLocation();
      if (!Loc.isValid()) Loc = C.getLoc();

      llvm::StringRef Result = Output.getStringRef();
      TheLexerWriter.Tokenize(getSourceLocation(
            SchirScheme->getFullSourceLocation(Loc)),
            Result);
      C.Cont();
    }));

    // Also provide a type erased LexerWriterFnRef which is
    // more suited to calling in c++.
    auto LexerWriterFn = [&](schir::SourceLocation Loc, llvm::StringRef Str) {
      TheLexerWriter.Tokenize(getSourceLocation(
            SchirScheme->getFullSourceLocation(Loc)), Str);
    };
    auto LWF = schir::LexerWriterFnRef(LexerWriterFn);
    SCHIR_CLANG_VAR(lexer_writer).set(Context, Context.CreateAny(LWF));

    // Do the thing.
    schir::TokenKind Terminator = schir::tok::r_brace;
    SchirScheme->ProcessTopLevelCommands(SchemeLexer, schir::builtins::eval,
                                         ErrorHandler, Terminator);

    // Return control to C++ Lexer
    PP.FinishEmbeddedLexer(SchemeLexer.GetByteOffset());
    if (!HasError)
      TheLexerWriter.FlushTokens();
    SCHIR_CLANG_VAR(lexer_writer).set(Context, schir::Undefined());
    SCHIR_CLANG_VAR(write_lexer).set(Context,
          Context.CreateBuiltin([](schir::Context& C, schir::ValueRefs Args) {
      C.RaiseError("lexer writer is not initialized");
    }));

    // The Lexers position has been changed
    // so we need to re-prime the look-ahead
    P.ConsumeAnyToken();
  }
};

} // namespace

static clang::PragmaHandlerRegistry::Add<SchirSchemePragmaHandler>
PragmaHandler("schir-scheme", "embed compile-time scheme");
