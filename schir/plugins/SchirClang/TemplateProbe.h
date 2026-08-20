// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_TEMPLATE_PROBE_H
#define SCHIRCLANG_TEMPLATE_PROBE_H

#include <clang/AST/DeclTemplate.h>
#include <clang/Sema/Sema.h>

#include "ClangUtil.h"

namespace schir_clang {

// Convert TemplateArgs to scheme values and cons the resulting list
// onto the front of Binding's value. C++ Types and other qualified
// names are converted to symbols.
static void
ProcessTemplateArgs(std::vector<std::string>& TemplateResults,
                    llvm::ArrayRef<clang::TemplateArgument> TemplateArgs) {
  llvm::SmallVector<schir::Value, 8> Values;
  auto VisitTA = [&](this auto&& Self,
                     clang::TemplateArgument const& TA) -> void {
    switch (TA.getKind()) {
    case clang::TemplateArgument::Type: {
        // Convert the type into a string of a fully qualified name.
        std::string TypeStr = TypeToString(TA.getAsType());
        TemplateResults.push_back(TypeStr);
        break;
      }
    case clang::TemplateArgument::Pack: {
        for (clang::TemplateArgument const& TA_ : TA.pack_elements())
          Self(TA_);
        break;
      }
    default: {
        // Unsupported.
        Values.push_back({});
        break;
      }
    }
  };
  for (clang::TemplateArgument const& TA : TemplateArgs)
    VisitTA(TA);
}

// Parse Expr and return a scheme list of lists with the template arguments
// of the instantiations of the class template identified by TemplateName.
void RunTemplateProbe(llvm::SmallVectorImpl<std::vector<std::string>>& Results,
                      std::string& ErrorMsg,
                      clang::Parser& P, schir::SchirScheme& HS,
                      llvm::BumpPtrAllocator& LexerSpellings,
                      schir::SourceLocation Loc,
                      llvm::StringRef TemplateName,
                      llvm::StringRef Expr) {
  clang::SourceLocation CLoc = getSourceLocation(HS.getFullSourceLocation(Loc));
  clang::ClassTemplateDecl*
  TemplateDecl = ParseSource(P, HS, LexerSpellings, Loc, TemplateName,
    [&] -> clang::ClassTemplateDecl* {
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
          /*AllowDestuctorName=*/false,
          /*AllowDeductionGuide=*/false,
          /*TemplateKWLoc=*/nullptr,
          UnqualifiedId);

      if (UnqualifiedId.getKind() != clang::UnqualifiedIdKind::IK_Identifier)
        return nullptr;
      clang::DeclarationName DeclName(UnqualifiedId.Identifier);
      clang::LookupResult LR(S, DeclName, CLoc,
                             clang::Sema::LookupOrdinaryName);
      S.LookupParsedName(LR, S.getCurScope(), &SS, clang::QualType());
      return LR.getAsSingle<clang::ClassTemplateDecl>();
    });

  if (!TemplateDecl) {
    ErrorMsg = "expecting class template name for probe";
    return;
  }

  // Run the Expr and then get the instantiations of templates.
  ParseExpression(P, HS, LexerSpellings, Loc, Expr);
  for (clang::ClassTemplateSpecializationDecl* Spec :
       TemplateDecl->specializations()) {
    Results.push_back({});
    ProcessTemplateArgs(Results.back(), Spec->getTemplateArgs().asArray());
  }
}

} // namespace schir_clang

#endif // SCHIRCLANG_TEMPLATE_PROBE_H
