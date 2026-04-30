//===-- PatternTemplate.h - Class for Syntax Pattern Matching ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Define schir::PatternTemplate mapping schir::Value to mlir::Value
//  for user defined syntax transformations via the `syntax-rules` syntax.
//  This file is provided as header-only to support OpGen.cpp.
//
//===----------------------------------------------------------------------===//

#include "TemplateGen.h"
#include "schir/Context.h"
#include "schir/OpGen.h"
#include "schir/Value.h"
#include "schir/ValueVisitor.h"
#include "llvm/Support/Casting.h"

namespace schir {

// PatternTemplate
//    - Generate code to match patterns and bind pattern variables
//      See R7RS 4.3.2 Pattern Language.
//    - Generate code for templates by visiting them with OpGen with
//      the pattern variables as SyntacticClosures (in TemplateGen)
class PatternTemplate : ValueVisitor<PatternTemplate, mlir::Value> {
  friend ValueVisitor<PatternTemplate, mlir::Value>;
  schir::OpGen& OpGen;
  Value Keyword;
  Value Ellipsis;
  NameSet& Literals;
  mlir::Value EnvArg;  // For syntax closures.
  llvm::SmallPtrSet<String*, 4> PatternVars;

  // P - the pattern node
  // E - the input to match

  SourceLocation getLoc() {
    return OpGen.getContext().getLoc();
  }

  mlir::Location createLoc(schir::SourceLocation Loc) {
    return mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                OpGen.Builder.getContext());
  }

  bool isEllipsis(schir::Value Id) {
    return equal(Id, Ellipsis);
  }

public:
  PatternTemplate(schir::OpGen& O,
                  schir::Value Keyword,
                  schir::Value Ellipsis,
                  mlir::Value EnvArg,
                  NameSet& Literals)
    : OpGen(O),
      Keyword(Keyword),
      Ellipsis(Ellipsis),
      Literals(Literals),
      EnvArg(EnvArg),
      PatternVars()
  {
    assert(isIdentifier(Keyword) && "expecting identifier");
    assert(isIdentifier(Ellipsis) && "expecting identifier");
  }

  // VisitPatternTemplate should be called with OpGen's insertion point in
  // the body of PatternOp
  mlir::Value VisitPatternTemplate(schir::Value Pattern,
                                   schir::Value Template,
                                   mlir::Value E) {
    schir::Context& Context = OpGen.getContext();
    EnvFrame* EF = Context.PushEnvFrame();
    schir::SourceLocation Loc = Pattern.getSourceLocation();
    if (isa_and_nonnull<Symbol>(Pattern.car())) {
      // Ignore the initial keyword.
      Pair* P = cast<Pair>(Pattern);
      auto MatchPairOp = OpGen.create<schir::MatchPairOp>(Loc, E);
      Visit(P->Cdr, MatchPairOp.getCdr());
    } else {
      Visit(Pattern, E);
    }

    if (!OpGen.CheckError()) {
      TemplateGen TG(OpGen, EnvArg, Keyword, PatternVars, Ellipsis);
      TG.BuildTemplate(Template);
    }

    Context.PopEnvFrame(EF);
    return mlir::Value();
  }

  mlir::Value bindPatternVar(Symbol* S, mlir::Value E) {
    bool Inserted;
    std::tie(std::ignore, Inserted) = PatternVars.insert(S->getString());
    if (!Inserted) {
      OpGen.SetError(
        "pattern variable name appears in pattern multiple times", S);
    }

    // Use any containing pair to carry source location information.
    mlir::Value SourceVal;
    if (auto MatchPairOp = E.getDefiningOp<schir::MatchPairOp>())
      SourceVal = MatchPairOp.getInput();
    else
      SourceVal = E;

    // Create a local variable with a SyntaxClosure of E as the initializer.
    schir::SourceLocation Loc = S->getSourceLocation();
    auto SynClo = OpGen.create<SyntaxClosureOp>(Loc, SourceVal, E, EnvArg);
    schir::Context& C = OpGen.getContext();
    Binding* B = C.CreateBinding(S, SynClo.getOperation());
    C.PushLocalBinding(B);
    return mlir::Value();
  }

  mlir::Value VisitValue(Value P, mlir::Value E) {
    // Disallow nodes that aren't explicitly allowed
    // in the specification. (r7rs 4.3.2)
    return OpGen.SetError("invalid pattern node", P);
  }

  mlir::Value VisitTail(schir::SourceLocation Loc,
                        Value P, mlir::Value E) {
    // Match the list after the `...`.
    uint32_t Length = 1;

    if (Pair* P2 = dyn_cast<Pair>(P))
      Loc = P2->getSourceLocation();

    schir::Value Cur = P;
    while (Pair* P2 = dyn_cast<Pair>(Cur)) {
      ++Length;
      Cur = P2->Cdr;
    }
    mlir::Value Tail = OpGen.create<MatchTailOp>(Loc, Length, E);

    // Actually match the tail part.
    Visit(P, Tail);

    // The tail is passed to the SubpatternOp.
    return Tail;
  }

  mlir::Value VisitSubpattern(schir::SourceLocation Loc,
                              Value P, Value Cdr, mlir::Value E) {
    // The Tail will be used as a sentinel value if it is a pair.
    mlir::Value Tail = VisitTail(Loc, Cdr, E);

    // Visit the subpattern.
    auto Body = std::make_unique<mlir::Region>();
    llvm::SmallVector<mlir::Value, 4> Packs;
    mlir::Block& Block = Body->emplaceBlock();
    {
      mlir::OpBuilder::InsertionGuard IG(OpGen.Builder);
      OpGen.Builder.setInsertionPointToStart(&Block);
      mlir::Type SchirValueT = OpGen.Builder.getType<SchirValueType>();
      mlir::Location MLoc = createLoc(Loc);
      // The block argument is the current pair from the input.
      mlir::Value BodyArg = Block.addArgument(SchirValueT, MLoc);
      auto PairVal = OpGen.create<MatchPairOp>(Loc, BodyArg);
      Visit(P, PairVal.getCar());

      // Create range for pack values by finding the
      // SyntaxClosureOps in the Body region.
      for (mlir::Operation& Op : Block) {
        if (auto SC = dyn_cast<SyntaxClosureOp>(&Op)) {
          Packs.push_back(SC.getResult());
        } else if (auto SP = dyn_cast<SubpatternOp>(&Op)) {
          for (mlir::Value V : SP.getResults())
            Packs.push_back(V);
        }
      }

      // Insert the terminator.
      OpGen.create<ResolveOp>(Loc, Packs);
    }

    // Create the SubpatternOp.
    OpGen.create<schir::SubpatternOp>(
        Loc, E, Tail, std::move(Body), Packs.size());

    // In the template, the nested syntax closures
    // will be looked up and check if its parent is
    // a subpattern op finding its result.
    return mlir::Value();
  }

  mlir::Value VisitPair(Pair* P, mlir::Value E) {
    // (<pattern>*)
    // (<pattern>* <pattern> <ellipsis> <pattern>*)
    schir::SourceLocation Loc = P->getSourceLocation();
    if (auto* P2 = dyn_cast<Pair>(P->Cdr);
        P2 && isEllipsis(P2->Car)) {
      // P->Car is the subpattern.
      // P2->Cdr is the rest of the pattern after the ...
      VisitSubpattern(Loc, P->Car, P2->Cdr, E);
    } else {
      auto M1 = OpGen.create<MatchPairOp>(Loc, E);
      Visit(P->Car, M1.getCar());
      if (!OpGen.CheckError())
        Visit(P->Cdr, M1.getCdr());
    }

    return mlir::Value();
  }

  mlir::Value VisitSymbol(Symbol* P, mlir::Value E) {
    // <underscore>
    if (P->Equiv("_")) {
      // Since _ always matches anything, there is
      // nothing to check.
      return mlir::Value();
    }

    // <pattern identifier> (literal identifier)
    if (Literals.contains(P->getString())) {
      SourceLocation Loc = P->getSourceLocation();
      EnvEntry Entry = OpGen.getContext().Lookup(P);
      if (!Entry) {
        // If the symbol is unbound just use the symbol.
        OpGen.create<schir::MatchOp>(Loc, P, E);
        return mlir::Value();
      } else {
        // FIXME This will create captures for local syntax which
        //       never need this. Exported syntax always refer to
        //       globals.
        // Match against the binding or instance of value itself.
        mlir::Value PV = OpGen.VisitEnvEntry(Loc, Entry);
        OpGen.create<schir::MatchIdOp>(Loc, PV, E);
        return mlir::Value();
      }
    }

    // <ellipsis>
    if (isEllipsis(P))
      return OpGen.SetError("<ellipsis> is not a valid pattern", P);

    // everything else is a pattern variable
    return bindPatternVar(P, E);
  }

  // Note: MatchOp is an implicitly chaining operation so
  // it has no result.

  mlir::Value VisitEmpty(Empty P, mlir::Value E) {
    OpGen.create<schir::MatchOp>(getLoc(), P, E);
    return mlir::Value();
  }

  mlir::Value VisitString(String* P, mlir::Value E) {
    // <pattern datum> -> <string>
    OpGen.create<schir::MatchOp>(getLoc(), P, E);
    return mlir::Value();
  }

  mlir::Value VisitBool(Bool P, mlir::Value E) {
    // <pattern datum> -> <boolean>
    OpGen.create<schir::MatchOp>(getLoc(), P, E);
    return mlir::Value();
  }

  mlir::Value VisitInt(Int P, mlir::Value E) {
    // <pattern datum> -> <number>
    OpGen.create<schir::MatchOp>(getLoc(), P, E);
    return mlir::Value();
  }

  mlir::Value VisitFloat(Float* P, mlir::Value E) {
    // <pattern datum> -> <number>
    OpGen.create<schir::MatchOp>(getLoc(), P, E);
    return mlir::Value();
  }
};

}
