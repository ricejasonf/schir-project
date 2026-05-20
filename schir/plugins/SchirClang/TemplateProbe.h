// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_TEMPLATE_PROBE_H
#define SCHIRCLANG_TEMPLATE_PROBE_H

#include <clang/Sema/Sema.h>
#include <clang/Sema/TemplateInstCallback.h>

#include "ClangUtil.h"

namespace schir_clang {

class TemplateProbeCallback : public clang::TemplateInstantiationCallback {
  clang::TypeAliasTemplateDecl* TemplateDecl;
  schir::Context& Context;
  schir::Binding* Binding;

public:
  TemplateProbeCallback(clang::TypeAliasTemplateDecl* TD,
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
    if (!Sema.CurContext->isDependentContext() &&
        CS.Kind == CS.TypeAliasTemplateInstantiation &&
        CS.Entity == TemplateDecl)
      ProcessTemplateArgs(CS);
  }

  void ProcessTemplateArgs(clang::Sema::CodeSynthesisContext const& CS) {
    // Get the template arguments and convert them to scheme values.
    // C++ Types and other qualified names are converted to symbols.
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
    for (clang::TemplateArgument const& TA : CS.template_arguments())
      VisitTA(TA);

    schir::Value Result = Context.CreateList(Values);
    schir::Value NewB = Context.CreatePair(Result, Binding->getValue());
    Binding->setValue(NewB);
  }
};

// Upon parsing Expr, create a scheme linked list of lists
// of c++ type template arguments of the instantiations of a
// type alias template identified by TemplateName. This will only include
// template instantiations that occur during the parsing of Expr.
// (Note that this will not include previously memoized instantiations.)
void RunTemplateProbe(clang::Parser& P, schir::SchirScheme& HS,
                      llvm::BumpPtrAllocator& LexerSpellings,
                      schir::Context& C,
                      schir::SourceLocation Loc,
                      llvm::StringRef TemplateName,
                      llvm::StringRef Expr) {
  clang::SourceLocation CLoc = getSourceLocation(HS.getFullSourceLocation(Loc));
  clang::TypeAliasTemplateDecl*
  TemplateDecl = ParseSource(P, HS, LexerSpellings, Loc, TemplateName,
    [&] -> clang::TypeAliasTemplateDecl* {
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
      return LR.getAsSingle<clang::TypeAliasTemplateDecl>();
    });

  if (!TemplateDecl)
    return C.RaiseError("expecting type alias template name for probe");

  // Prepare to record all the instantiations of Template
  // via template inst callback or whatever. The result
  // is a scheme list of lists saved to the scheme binding.
  clang::Sema& Sema = P.getActions();
  schir::Binding* B = C.CreateBinding(schir::Empty());
  // Track the pointer to compare when removing the callback
  // from the vector.
  auto* CB_ptr = new TemplateProbeCallback(TemplateDecl, C, B);
  auto CB =
    std::unique_ptr<clang::TemplateInstantiationCallback>(
        CB_ptr);
  Sema.TemplateInstCallbacks.push_back(std::move(CB));

  ParseExpression(P, HS, LexerSpellings, Loc, Expr);

  // Remove template inst callback.
  llvm::erase_if(Sema.TemplateInstCallbacks,
    [CB_ptr](auto const& CB) {
      return CB.get() == CB_ptr;
    });

  C.Cont(B->getValue());
}

} // namespace schir_clang

#endif // SCHIRCLANG_TEMPLATE_PROBE_H
