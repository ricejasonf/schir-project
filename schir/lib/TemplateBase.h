//===-- TemplateBase.h - Class for Syntax Pattern Matching ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Define schir::TemplateBase mapping schir::Value to mlir::Value
//  for user defined syntax transformations via the `syntax-rules` syntax.
//  This file is provided as header-only to support OpGen.cpp.
//
//===----------------------------------------------------------------------===//

#include <schir/Context.h>
#include <schir/OpGen.h>
#include <schir/Value.h>
#include <schir/ValueVisitor.h>
#include <mlir/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <variant>

namespace schir {

// Literals that are not affected by transformations
// can be returned as schir::Value to prevent having
// to create an operation for contructing every part of
// every literal.
// (ie Keep the LiteralOps as large chunks of scheme AST.)
struct TemplateError { };
using TemplateResult = std::variant<TemplateError, mlir::Value,
                                    schir::Value>;

// TemplateBase
//    - Provide a base class for quasiquote and syntax-rules
//      templates. This holds the reference to OpGen and provides
//      utility functions for creating operations that build
//      literals interspersed with values.
template <typename Derived>
class TemplateBase : protected ValueVisitor<Derived, TemplateResult> {

public:
  // Template is the input "form" with syntax closures.
  mlir::Value transformSyntax(schir::Value Template) {
    TemplateResult Result = getDerived().Visit(Template);

    mlir::Value TransformedSyntax;
    if (mlir::Value* MVP = std::get_if<mlir::Value>(&Result))
      TransformedSyntax = *MVP;
    else
      TransformedSyntax = this->createLiteral(std::get<schir::Value>(Result));
    return TransformedSyntax;
  }

  // Create a new environment with the renamed name/values in it.
  // (They will have their original names in the "renamed environment".)
  mlir::Value finishRenameEnv() {
    schir::SourceLocation Loc;
    // Create the RenameOps.
    llvm::SmallVector<mlir::Value, 12> RenameOpVector;
    for (auto [Str, Pair] : RenameOps) {
      auto [R, SC] = Pair;
      // Skip an identifier of a local syntax, which has no rename.
      if (!R)
        continue;
      // Move RenameOps to the beginning of the block.
      R.getDefiningOp()->moveBefore(RenameEnv);
      RenameOpVector.push_back(R);
    }

    // Add the RenameOps to a new RenameEnvOp
    // to serve as the base of the environment.
    mlir::OpBuilder::InsertionGuard IG(OpGen.Builder);
    OpGen.Builder.setInsertionPoint(RenameEnv);
    auto NewRenameEnv = OpGen.create<RenameEnvOp>(Loc, RenameEnv.getParent(),
                                                  RenameOpVector);
    RenameEnv.getResult().replaceAllUsesWith(NewRenameEnv);
    RenameEnv->erase();
    RenameEnv = NewRenameEnv;
    return NewRenameEnv;
  }

  // Compile the result (at the "comile-time"'s "run-time").
  void createOpGen(schir::SourceLocation Loc, mlir::Value Expr) {
    OpGen.createOpGen(Loc, Expr);
  }


protected:
  friend ValueVisitor<Derived, TemplateResult>;
  using ValueVisitor<Derived, TemplateResult>::getDerived;

  schir::OpGen& OpGen;
  schir::RenameEnvOp RenameEnv;
  // Map the results of RenameOps.
  llvm::DenseMap<String*, std::pair<mlir::Value, mlir::Value>> RenameOps;
  // Map the closing env and node of closed identifiers to their renames.
  llvm::DenseMap<std::pair<uintptr_t, uintptr_t>, mlir::Value> ClosedRenames;

  // Scope is the definition frame of a local syntax (or null).
  TemplateBase(schir::OpGen& O, mlir::Value Scope = mlir::Value())
    : OpGen(O),
      RenameEnv(O.create<RenameEnvOp>(schir::SourceLocation(), Scope,
                                      llvm::ArrayRef<mlir::Value>()))
  { }

  mlir::Location createLoc(schir::SourceLocation Loc) {
    return mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                OpGen.Builder.getContext());
  }

  // Get the syntax function that contains the insertion point.
  schir::FuncOp getSyntaxFunction() {
    mlir::Operation* Op = OpGen.Builder.getBlock()->getParentOp();
    if (auto FuncOp = dyn_cast<schir::FuncOp>(Op))
      return FuncOp;
    return Op->getParentOfType<schir::FuncOp>();
  }

  // createValue - Create a mlir::Value from its stored
  //               representation in schir::Value or wrap
  //               it in a LiteralOp.
  mlir::Value createValue(TemplateResult Input) {
    if (mlir::Value* MV = std::get_if<mlir::Value>(&Input))
      return *MV;

    if (schir::Value* V = std::get_if<schir::Value>(&Input)) {
      mlir::Value MV = OpGen::toValue(*V);
      return MV ? MV : createLiteral(*V);
    }

    // TemplateError
    return OpGen.createUndefined();
  }

  mlir::Value createLiteral(schir::Value V) {
    return OpGen.create<LiteralOp>(V.getSourceLocation(), V);
  }

  schir::ConsOp createList(SourceLocation Loc, schir::Value X,
                              schir::Value Y) {
    mlir::Value ValX = createValue(X);
    mlir::Value ValY = createValue(Y);
    mlir::Value Empty = createLiteral(schir::Empty{});
    return OpGen.create<ConsOp>(Loc, ValX,
        OpGen.create<ConsOp>(Loc, ValY, Empty));
  }

  mlir::Value createCons(SourceLocation Loc, TemplateResult const& X,
                                             TemplateResult const& Y) {
    mlir::Value ValX = createValue(X);
    mlir::Value ValY = createValue(Y);
    return OpGen.create<ConsOp>(Loc, ValX, ValY);
  }

  mlir::Value createSplice(SourceLocation Loc, TemplateResult const& X,
                                               TemplateResult const& Y) {
    mlir::Value ValX = createValue(X);
    mlir::Value ValY = createValue(Y);
    return OpGen.create<SpliceOp>(Loc, ValX, ValY);
  }

  // Specifying ProvidedVal forces the value into the "rename environment".
  mlir::Value createRename(Symbol* P, mlir::Value ProvidedVal = nullptr) {
    // Since RenameOps are memoized, every operation
    // inserted in this function needs to have the FuncOp
    // as its parent operation.
    mlir::OpBuilder::InsertionGuard IG(OpGen.Builder);
    String* UniqueStr = P->getString();

    // Check for memoized SC.
    if (!ProvidedVal) {
      auto [R, SC] = RenameOps.lookup(UniqueStr);
      if (SC)
        return SC;
    }

    // The result will be a symbol literal.
    // The built RenameOps are added to the RenameOps list.
    SourceLocation Loc = P->getSourceLocation();
    llvm::StringRef Id = P->getStringRef();

    // Insert Literals and SyntaxClosures after the RenameEnvOp
    OpGen.Builder.setInsertionPointAfter(RenameEnv);

    mlir::Value CaptureVal = ProvidedVal ? ProvidedVal : createLiteral(P);
    mlir::Value SourceVal = CaptureVal;
    mlir::Value SC;
    {
      if (!isa<SchirSyntaxType>(CaptureVal.getType()))
        SC = OpGen.create<SyntaxClosureOp>(Loc, SourceVal, CaptureVal,
                                           RenameEnv);
    }

    if (ProvidedVal) {
      mlir::Value R = OpGen.create<schir::RenameOp>(Loc, Id, CaptureVal);
      RenameOps.insert({UniqueStr, {R, SC}});
      return SC;
    }

    // Close an identifier of a local syntax in its definition frame.
    if (RenameEnv.getParent()) {
      RenameOps.insert({UniqueStr, {mlir::Value(), SC}});
      return SC;
    }

    // Insert RenameOps before the RenameEnvOp.
    OpGen.Builder.setInsertionPoint(RenameEnv);

    // Rename to a global.
    mlir::Value R = createRenameGlobal(P, OpGen.getContext().Lookup(P));
    if (!R)
      return OpGen.createUndefined();
    RenameOps.insert({UniqueStr, {R, SC}});
    return SC;
  }

  // Close an identifier of a local syntax in its definition frame, or
  // rename an identifier of a global syntax in a rename env of its own.
  mlir::Value createClosedRename(SyntaxClosure* ClosedId) {
    if (OpGen.CheckError())
      return OpGen.createUndefined();
    mlir::OpBuilder::InsertionGuard IG(OpGen.Builder);
    Symbol* P = unwrapIdentifier(ClosedId);
    // Identify a closed symbol by its name.
    Value Node = ClosedId->Node;
    if (auto* S = dyn_cast<Symbol>(Node))
      Node = S->getString();
    auto Key = std::make_pair(ClosedId->Env.getOpaqueValue(),
                              Node.getOpaqueValue());
    if (mlir::Value SC = ClosedRenames.lookup(Key))
      return SC;

    SourceLocation Loc = P->getSourceLocation();
    mlir::Value SC;
    if (RenameEnv.getParent()) {
      // Insert the operations after the RenameEnvOp.
      OpGen.Builder.setInsertionPointAfter(RenameEnv);
      mlir::Value Id = captureClosedId(ClosedId);
      SC = OpGen.create<SyntaxClosureOp>(Loc, Id, Id, RenameEnv,
                                         /*Reclose=*/true);
    } else {
      // Insert the operations before the RenameEnvOp.
      OpGen.Builder.setInsertionPoint(RenameEnv);
      EnvEntry Entry = OpGen.LookupEnv(ClosedId);
      if (Entry && !Entry.MangledName)
        return OpGen.SetError("unexpected closed identifier of a local",
                              ClosedId);
      mlir::Value R = createRenameGlobal(P, Entry);
      if (!R)
        return OpGen.createUndefined();
      mlir::Value Env = OpGen.create<RenameEnvOp>(Loc, mlir::Value(), R);
      mlir::Value Node = createLiteral(P);
      SC = OpGen.create<SyntaxClosureOp>(Loc, Node, Node, Env);
    }
    ClosedRenames.insert({Key, SC});
    return SC;
  }

  // Load a closed identifier that a local syntax captures.
  mlir::Value captureClosedId(SyntaxClosure* ClosedId) {
    llvm::SmallVectorImpl<Value>* Captures = OpGen.LocalSyntaxCaptures;
    assert(Captures && "local syntax should collect captures");
    mlir::Value Closure = getSyntaxFunction().getBody().front().getArgument(0);
    uint32_t Index = Captures->size();
    Captures->push_back(ClosedId);
    return OpGen.create<LoadRefOp>(ClosedId->getSourceLocation(), Closure,
                                   Index);
  }

  // Rename an identifier to the global of Entry or of its name.
  mlir::Value createRenameGlobal(Symbol* P, EnvEntry Entry) {
    std::string MangledNameStr;
    llvm::StringRef MangledName;
    if (Entry.MangledName) {
      MangledName = Entry.MangledName->getStringRef();
    } else {
      // Default to the name of a global.
      schir::Mangler Mangler = OpGen.createMangler();
      MangledNameStr = Mangler.mangleVariable(OpGen.getModulePrefix(), P);
      if (MangledNameStr.empty())
        return mlir::Value();
      MangledName = llvm::StringRef(MangledNameStr);
    }

    assert(!MangledName.empty() && "should have mangled name");
    return OpGen.create<RenameGlobalOp>(P->getSourceLocation(),
                                        P->getStringRef(), MangledName);
  }

  // ValueVisitor functions

  TemplateResult VisitValue(schir::Value P) {
    return P;
  }

  TemplateResult VisitSyntaxClosure(SyntaxClosure* SC) {
    mlir::Value EnvVal = any_cast<mlir::Value>(SC->Env);
    if (!EnvVal)
      return OpGen.SetError("expecting env mlir.value");

    if (!isa<Symbol>(SC->Node))
      return OpGen.SetError("expecting a symbol");

    schir::SourceLocation Loc = SC->Node.getSourceLocation();
    mlir::Value Node = createLiteral(SC->Node);
    mlir::Value SourceVal = Node;
    return OpGen.create<SyntaxClosureOp>(Loc, SourceVal, Node, EnvVal);
  }

  TemplateResult VisitPair(Pair* P) {
    schir::SourceLocation Loc = P->getSourceLocation();

    TemplateResult CarResult = getDerived().Visit(P->Car);
    TemplateResult CdrResult = getDerived().Visit(P->Cdr);

    // If nothing changed
    auto* HCar = std::get_if<schir::Value>(&CarResult);
    auto* HCdr = std::get_if<schir::Value>(&CdrResult);
    if (HCar && HCdr && *HCar == P->Car && *HCdr == P->Cdr)
      return P;

    if (OpGen.CheckError())
      return OpGen.createUndefined();

    return createCons(Loc, CarResult, CdrResult);
  }

  TemplateResult VisitSymbol(Symbol* P) = delete;
};

}
