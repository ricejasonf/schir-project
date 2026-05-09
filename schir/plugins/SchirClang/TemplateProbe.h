// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_TEMPLATE_PROBE_H
#define SCHIRCLANG_TEMPLATE_PROBE_H

#include <clang/Sema/Sema.h>
#include <clang/Sema/TemplateInstCallback.h>

#include "ClangUtil.h"

namespace schir_clang {

class TemplateProbeCallback : public clang::TemplateInstantiationCallback {
  clang::TemplateDecl* TemplateDecl;
  schir::Context& Context;
  schir::Binding* Binding;

public:
  TemplateProbeCallback(clang::TemplateDecl* TD,
                        schir::Context& C, schir::Binding* B)
    : TemplateDecl(TD),
      Context(C),
      Binding(B)
  { }

  void initialize(clang::Sema const& Sema) override {
    // Do nothing.
  }

  void finalize(clang::Sema const& Sema) override {
    // Do nothing.
  }

  void atTemplateBegin(clang::Sema const& Sema,
          clang::Sema::CodeSynthesisContext const& CS) override {
    // Do nothing.
  }

  void atTemplateEnd(clang::Sema const& Sema,
          clang::Sema::CodeSynthesisContext const& CS) override {
    //if (CS.Kind = clang::Sema::CodeSynthesisContext::TemplateInstantiation &&
    if (CS.Kind == CS.TemplateInstantiation && CS.Template == TemplateDecl)
      ProcessTemplateArgs(CS);
  }

  void ProcessTemplateArgs(clang::Sema::CodeSynthesisContext const& CS) {
    // Get the template arguments and convert them to scheme values.
    // C++ Types and other qualified names are converted to symbols.
    llvm::SmallVector<schir::Value, 8> Values;
    for (clang::TemplateArgument const& TA : CS.template_arguments()) {
      switch (TA.getKind()) {
      case clang::TemplateArgument::Type: {
          // Convert the type into a string of a fully qualified name.
          std::string TypeStr = TypeToString(TA.getAsType());
          schir::Value Str = Context.CreateString(TypeStr);
          Values.push_back(Str);
          break;
        }
      default: {
          // Unsupported.
          Values.push_back(schir::Undefined());
          break;
        }
      }
    }

    schir::Value Result = Context.CreateList(Values);
    schir::Value NewB = Context.CreatePair(Result, Binding->getValue());
    Binding->setValue(NewB);
  }
};

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

  // TODO
  // Prepare to record all the instantiations of Template
  // via template inst callback or whatever.

  //ParseExpression(P, HS, Loc, Expr);

  // TODO Remove template inst callback or whatever.
}

} // namespace schir_clang

#endif // SCHIRCLANG_TEMPLATE_PROBE_H
