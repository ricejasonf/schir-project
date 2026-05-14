// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_CLANGUTIL_H
#define SCHIRCLANG_CLANGUTIL_H

#include <clang/AST/Expr.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Pragma.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/EnterExpressionEvaluationContext.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>

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
  // We typically need to have an evaluated context to
  // instantiate dependent lambdas and such.
  clang::EnterExpressionEvaluationContext EvalCtx(
      P.getActions(), clang::Sema::ExpressionEvaluationContext::ConstantEvaluated);
  return ParseSource(P, HS, Loc, Source, [&] {
    // Parse the expression.
    return P.ParseExpression();
  });
}

// FIXME Weird error assuming missing > to match nonexistant < (I guess.)
clang::TypeResult ParseTypeName(clang::Parser& P, schir::SchirScheme& HS,
                                schir::SourceLocation Loc,
                                llvm::StringRef Source) {
  return ParseSource(P, HS, Loc, Source, [&] {
    // Parse the expression.
    return P.ParseTypeName();
  });
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
