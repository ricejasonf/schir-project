// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_CLANGUTIL_H
#define SCHIRCLANG_CLANGUTIL_H

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Pragma.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/EnterExpressionEvaluationContext.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/Twine.h>
#include <optional>

namespace schir_clang {
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

template <typename Fn>
auto ParseSource(clang::Parser& P, schir::SchirScheme& HS,
                 llvm::BumpPtrAllocator& LexerSpellings,
                 schir::SourceLocation Loc,
                 llvm::StringRef Source,
                 Fn&& Thunk) {
  // Lex and expand.
  LexerWriter TheLexerWriter(P, LexerSpellings);
  TheLexerWriter.Tokenize(getSourceLocation(HS.getFullSourceLocation(Loc)),
                          Source);
  TheLexerWriter.PushEod();
  TheLexerWriter.FlushTokens();

  P.ConsumeAnyToken();
  return Thunk();
}

clang::ExprResult ParseExpression(clang::Parser& P, schir::SchirScheme& HS,
                                  llvm::BumpPtrAllocator& LexerSpellings,
                                  schir::SourceLocation Loc,
                                  llvm::StringRef Source) {
  // We typically need to have an evaluated context to
  // instantiate dependent lambdas and such.
  clang::EnterExpressionEvaluationContext EvalCtx(
      P.getActions(),
      clang::Sema::ExpressionEvaluationContext::ConstantEvaluated);
  clang::ExprResult Result = ParseSource(P, HS, LexerSpellings,
                                         Loc, Source, [&] {
    // Parse the expression.
    return P.ParseExpression();
  });
  return Result;
}

// FIXME Weird error assuming missing > to match nonexistant < (I guess.)
clang::TypeResult ParseTypeName(clang::Parser& P, schir::SchirScheme& HS,
                                llvm::BumpPtrAllocator& LexerSpellings,
                                schir::SourceLocation Loc,
                                llvm::StringRef Source) {
  return ParseSource(P, HS, LexerSpellings, Loc, Source, [&] {
    // Parse the expression.
    return P.ParseTypeName();
  });
}

std::optional<uint64_t> GetStdArrayCharSize(clang::QualType QT) {
  QT = QT.getCanonicalType();
  clang::CXXRecordDecl const* RD = QT->getAsCXXRecordDecl();
  if (!RD || !RD->isInStdNamespace())
    return std::nullopt;
  clang::IdentifierInfo const* II = RD->getIdentifier();
  if (!II || !II->isStr("array"))
    return std::nullopt;
  auto const* CTSD =
    clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(RD);
  if (!CTSD)
    return std::nullopt;
  clang::TemplateArgumentList const& Args = CTSD->getTemplateArgs();
  if (Args.size() != 2 ||
      Args[0].getKind() != clang::TemplateArgument::Type ||
      !Args[0].getAsType()->isCharType() ||
      Args[1].getKind() != clang::TemplateArgument::Integral)
    return std::nullopt;
  return Args[1].getAsIntegral().getZExtValue();
}

bool IsStdStringView(clang::QualType QT) {
  QT = QT.getCanonicalType();
  clang::CXXRecordDecl const* RD = QT->getAsCXXRecordDecl();
  if (!RD || !RD->isInStdNamespace())
    return false;
  clang::IdentifierInfo const* II = RD->getIdentifier();
  if (!II || !II->isStr("basic_string_view"))
    return false;
  auto const* CTSD =
    clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(RD);
  if (!CTSD)
    return false;
  clang::TemplateArgumentList const& Args = CTSD->getTemplateArgs();
  return Args.size() >= 1 &&
         Args[0].getKind() == clang::TemplateArgument::Type &&
         Args[0].getAsType()->isCharType();
}

// Evaluate an expression of type std::array<char, N>.
schir::Value EvalArrayCharExpr(clang::Parser& P, schir::SchirScheme& HS,
                               std::string& ErrorMsg,
                               clang::Expr* Expr, uint64_t N) {
  clang::Expr::EvalResult EvalResult;
  if (!Expr->EvaluateAsRValue(EvalResult,
        P.getActions().getASTContext(), /*InConstantContext=*/true)) {
    ErrorMsg = "clang std::array<char, N> expr evaluation failed";
    return nullptr;
  }

  schir::Context& Context = HS.getContext();
  if (N == 0)
    return Context.CreateString(llvm::StringRef());

  // std::array<char, N> is an aggregate with a single data member.
  clang::APValue& ArrVal = EvalResult.Val.getStructField(0);
  unsigned NumInit = ArrVal.getArrayInitializedElts();
  llvm::SmallString<64> Chars;
  Chars.reserve(N);
  for (uint64_t I = 0; I < N; ++I) {
    clang::APValue& Elt = I < NumInit
      ? ArrVal.getArrayInitializedElt(I)
      : ArrVal.getArrayFiller();
    Chars.push_back(static_cast<char>(Elt.getInt().getExtValue()));
  }

  // Take everything before null terminator.
  llvm::StringRef Str(Chars);
  return Context.CreateString(Str.take_front(Str.find('\0')));
}

// Evaluate expr of type std::string_view.
schir::Value EvalStringViewExpr(clang::Parser& P, schir::SchirScheme& HS,
                                llvm::BumpPtrAllocator& LexerSpellings,
                                std::string& ErrorMsg,
                                schir::SourceLocation Loc,
                                llvm::StringRef Expr) {
  // Wrap string_view to a known layout for destructuring.
  std::string WrappedExprStr =
    llvm::Twine("[]{ auto&& s = (" + Expr +
                "); return std::pair<char const*, unsigned long>("
                "s.data(), s.size()); }()").str();
  clang::ExprResult ExprResult = ParseExpression(P, HS, LexerSpellings,
                                                 Loc, WrappedExprStr);
  if (ExprResult.isInvalid()) {
    ErrorMsg = "clang std::string_view wrapper expr parse failed";
    return nullptr;
  }
  clang::Expr* WrappedExpr = ExprResult.get();

  clang::Expr::EvalResult EvalResult;
  if (!WrappedExpr->EvaluateAsRValue(EvalResult,
        P.getActions().getASTContext(), /*InConstantContext=*/true)) {
    ErrorMsg = "clang string_view expr evaluation failed";
    return nullptr;
  }

  // The wrapped expression has type std::pair<char const*, unsigned long>
  clang::APValue& StructVal = EvalResult.Val;
  uint64_t Len = StructVal.getStructField(1).getInt().getZExtValue();

  schir::Context& Context = HS.getContext();
  if (Len == 0)
    return Context.CreateString(llvm::StringRef());

  clang::APValue& PtrVal = StructVal.getStructField(0);
  if (!PtrVal.isLValue()) {
    ErrorMsg = "expr-eval string_view invalid ptr";
    return nullptr;
  }
  clang::APValue::LValueBase Base = PtrVal.getLValueBase();
  clang::Expr const* BaseExpr = Base.dyn_cast<clang::Expr const*>();
  clang::StringLiteral const* SL = BaseExpr
    ? clang::dyn_cast<clang::StringLiteral>(BaseExpr) : nullptr;
  if (!SL || SL->getCharByteWidth() != 1) {
    ErrorMsg = "expr-eval string_view invalid";
    return nullptr;
  }

  llvm::SmallString<64> Chars;
  Chars.reserve(Len);
  uint64_t Offset = static_cast<uint64_t>(PtrVal.getLValueOffset()
                                                .getQuantity());
  for (uint64_t I = 0; I < Len; ++I) {
    uint64_t Index = Offset + I;
    char C = Index < SL->getLength()
      ? static_cast<char>(SL->getCodeUnit(Index)) : '\0';
    Chars.push_back(C);
  }

  return Context.CreateString(llvm::StringRef(Chars));
}

// Print the canonical type with cvref qualifiers stripped.
// Note that anonymous namespace information is also lost.
std::string TypeToString(clang::QualType QT) {
  QT = QT.getCanonicalType()
         .getNonReferenceType()
         .getUnqualifiedType();
  clang::LangOptions LO;
  clang::PrintingPolicy PP(LO);
  PP.PrintAsCanonical = true;
  PP.SuppressUnwrittenScope = true;
  PP.SuppressTagKeyword = true;
  return QT.getAsString(PP);
}
} // namespace schir_clang
#endif // SCHIRCLANG_CLANGUTIL_H
