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
void ProcessTemplateArgs(llvm::ArrayRef<clang::TemplateArgument> TemplateArgs,
                         schir::Context& Context,
                         schir::Binding* Binding) {
  llvm::SmallVector<schir::Value, 8> Values;
  schir::Context& C = Context; // Avoid Clang ICE.
  auto VisitTA = [&](this auto&& Self,
                     clang::TemplateArgument const& TA) -> void {
    switch (TA.getKind()) {
    case clang::TemplateArgument::Type: {
        // Convert the type into a string of a fully qualified name.
        std::string TypeStr = TypeToString(TA.getAsType());
        schir::Value Str = C.CreateSymbol(TypeStr);
        Values.push_back(Str);
        break;
      }
    case clang::TemplateArgument::Pack: {
        for (clang::TemplateArgument const& TA_ : TA.pack_elements())
          Self(TA_);
        break;
      }
    default: {
        TA.dump();
        // Unsupported.
        Values.push_back(schir::Undefined());
        break;
      }
    }
  };
  for (clang::TemplateArgument const& TA : TemplateArgs)
    VisitTA(TA);

  schir::Value Result = Context.CreateList(Values);
  schir::Value NewB = Context.CreatePair(Result, Binding->getValue());
  Binding->setValue(NewB);
}

// Parse Expr and return a scheme list of lists with the template arguments
// of the instantiations of the class template identified by TemplateName.
void RunTemplateProbe(clang::Parser& P, schir::SchirScheme& HS,
                      llvm::BumpPtrAllocator& LexerSpellings,
                      schir::Context& C,
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
      return LR.getAsSingle<clang::ClassTemplateDecl>();
    });

  if (!TemplateDecl)
    return C.RaiseError("expecting class template name for probe");

  // Prepare to record all the instantiations of TemplateName triggered
  // while parsing Expr. The result is a scheme list of lists saved to
  // the scheme binding.
  schir::Binding* B = C.CreateBinding(schir::Empty());

  ParseExpression(P, HS, LexerSpellings, Loc, Expr);

  for (clang::ClassTemplateSpecializationDecl* Spec :
       TemplateDecl->specializations())
    ProcessTemplateArgs(Spec->getTemplateArgs().asArray(), C, B);

  C.Cont(B->getValue());
}

} // namespace schir_clang

#endif // SCHIRCLANG_TEMPLATE_PROBE_H
