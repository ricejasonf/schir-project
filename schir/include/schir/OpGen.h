//===------ OpGen.h - Classes for generating MLIR Operations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines schir::OpGen for syntax expansion and operation generation
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_OP_GEN_H
#define LLVM_SCHIR_OP_GEN_H

#include "schir/Context.h"
#include "schir/Dialect.h"
#include "schir/Mangle.h"
#include "schir/Value.h"
#include "schir/ValueVisitor.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h" // function_ref
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"
#include <deque>
#include <string>
#include <tuple>
#include <utility>

namespace mlir {
  class MLIRContext;
  class OwningModuleRef;
  class Value;
}

namespace schir {
using IdSet = std::set<std::pair<uintptr_t, uintptr_t>>;
using NameSet = llvm::SmallPtrSetImpl<String*>;
class PatternTemplate;
class TemplateGen;

template <typename Derived>
class TemplateBase;

class OpGen : public ValueVisitor<OpGen, mlir::Value> {
  friend ValueVisitor;
  friend CopyCollector;
  friend PatternTemplate;
  friend TemplateGen;
  template <typename Derived>
  friend class TemplateBase;
  using BindingScopeTable = llvm::ScopedHashTable<
                                            schir::Value,
                                            mlir::Value>;
  using BindingScope = typename BindingScopeTable::ScopeTy;

  struct LambdaScope;
  struct LambdaScopeNode {
    mlir::Operation* Op; // The function of the continuation.
    mlir::Operation* CallOp = nullptr;  // The call to insert PushCont before.
    llvm::SmallVector<mlir::Value, 8> Captures;
    BindingScope BindingScope_;

    LambdaScopeNode(mlir::Operation* Op,
                    mlir::Operation* CallOp,
                    BindingScopeTable& Table)
      : Op(Op),
        CallOp(CallOp),
        Captures(),
        BindingScope_(Table)
    { }
  };

  // Continuation scopes get popped by their containing
  // lambda. The PushContOp is inserted right before the
  // specified CallOp.
  void PushContinuationScope(mlir::Operation* Op, mlir::Operation* CallOp) {
    LambdaScopes.emplace_back(Op, CallOp, BindingTable);
  }

  // pop the scope and build the PushContOp with its captures
  void PopContinuationScope();

  void setTopLevelOp(mlir::Operation* Op) {
    assert(LambdaScopes.size() == 1 &&
        "TopLevelOp should be at module scope with a LambdaScope");
    LambdaScopes.emplace_back(Op, nullptr, BindingTable);
    TopLevelOp = Op;
  }

  using LambdaScopeIterator = typename std::deque<LambdaScopeNode>
                                ::reverse_iterator;

  schir::Context& Context;

  // ModuleBuilder - Contain insertion point for top level operations
  //                 in the output ModuleOp
  mlir::OpBuilder ModuleBuilder;

  // Builder - Contain the current insertion point for non-top-level
  //           operations.
  mlir::OpBuilder Builder;

  BindingScopeTable BindingTable;
  std::deque<LambdaScopeNode> LambdaScopes;

  // Map an export target symbol name to an ExportIdOp.
  llvm::DenseMap<schir::String*, schir::ExportIdOp> Exports;
  // Map environment name to renamed exported name.
  std::vector<std::pair<schir::String*, schir::String*>> RenameExports;

  // ModuleOp is the current output module for a Program/Library
  // It is always nested within the topmost module which also
  // contains top-level operations imported from Scheme modules.
  mlir::Operation* ModuleOp;

  // TopLevelOp The current top level operation being generated.
  //            It may be either CommandOp, GlobalOp, nullptr.
  mlir::Operation* TopLevelOp = nullptr;

  // TopLevelHandler - Use to evaluate potentially multiple top level
  //                   operations. The handler should receive a single
  //                   Value that is an Operation.
  //                   TODO This needs to be visited during GC.
  Value TopLevelHandler = nullptr;

  // LibraryEnvProc - In the context of define-library we allow <library spec>
  //                  to enter the library environment. No operations should
  //                  be inserted if this is set.
  Binding* LibraryEnvProc = nullptr;

  // The current syntax closure will affect how we perform lookup
  // in LookupEnv.
  SyntaxClosure* CurSyntaxClosure = nullptr;

  struct SyntaxClosureScope;

  // Err - The stored error to indicate that the compiler is in an error state.
  //       (Use CheckError())
  Value Err = nullptr;
  // Track depth of calls to RunSync to allow unwinding errors properly.
  size_t RunSyncDepth = 0;

  bool IsLocalDefineAllowed = false;
  schir::Symbol* ModulePrefix = nullptr;
  unsigned LambdaNameCount = 0;
  bool IsTailPos = true;

  struct TailPosScope {
    bool& State;
    bool PrevState;

    TailPosScope(schir::OpGen& OpGen)
      : State(OpGen.IsTailPos),
        PrevState(State)
    { }

    TailPosScope(TailPosScope&) = delete;

    ~TailPosScope() {
      State = PrevState;
    }
  };

  void InsertTopLevelCommandOp(SourceLocation Loc);
  bool WalkDefineInits(Value Env, IdSet& LocalNames);
  void FinishLocalDefines();

public:
  explicit OpGen(schir::Context& C, schir::Symbol* ModulePrefix = nullptr);
  ~OpGen();

  schir::Context& getContext() { return Context; }

  // CheckError
  //  - Returns true if there is an error or exception
  //  - Builtins will have to check this to stop evaluation
  //    when errors occur
  bool CheckError() const {
    return Err ? true : false;
  }

  void ClearError() { Err = nullptr; }


  mlir::ModuleOp getModuleOp();
  
  mlir::Value CheckType(schir::SourceLocation Loc, mlir::Value V,
                        mlir::Type Type);

  // Visit a node expecting a single result
  mlir::Value GetSingleResultOrBinding(schir::Value V);
  // Localize and unwrap a single result.
  mlir::Value GetSingleResult(schir::Value V);

  mlir::Value UnwrapBinding(mlir::Value MV);

  llvm::StringRef getModulePrefix() {
    if (!ModulePrefix || ModulePrefix->getStringRef().empty()) {
      return schir::Mangler::getManglePrefix();
    }
    return ModulePrefix->getStringRef();
  }

  void UpdateExports(String* Name, String* MangledName);
  void Export(Value NameList);
  void Import(ImportSet*);
  void ImportValue(Environment* Env, String* Id,
                   String* MangledName);
  void VisitLibrary(schir::SourceLocation Loc, schir::Symbol* MangledName,
                    schir::Value LibraryDecls);
  void FinishLibrary();
  void VisitTopLevel(Value V);
  void FinishTopLevelOp();
  void VisitTopLevelSequence(Value List);
  void SetTopLevelHandler(Value OnTopLevel) {
    if (isa<Undefined>(OnTopLevel)) {
      TopLevelHandler = nullptr;
    } else {
      TopLevelHandler = OnTopLevel;
    }
  }
  void VisitLibrarySpec(Value V);
  void WithLibraryEnv(Value Thunk);
  bool isLibraryContext() {
    return LibraryEnvProc != nullptr;
  }

  bool isTopLevel() { return TopLevelOp == nullptr; }
  bool isTailPos() { return IsTailPos; }
  bool isLocalDefineAllowed();

  std::string mangleModule(schir::Value Name);
  std::string mangleFunctionName(llvm::StringRef Name);
  std::string mangleVariable(schir::Value Name);
  std::string mangleSyntax(schir::Value Name);

  // createHelper - Facilitate creating an operation with proper source
  //                location information.
  //              - Calling this directly bypasses implicit wrapping in
  //                a top level op as well as finalizing local defines.
  template <typename Op, typename ...Args>
  static Op createHelper(mlir::OpBuilder& Builder, schir::SourceLocation Loc,
                         Args&& ...args) {
    assert(Builder.getInsertionBlock() != nullptr &&
        "Operation must have insertion point");
    mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                               Builder.getContext());
    return Op::create(Builder, MLoc, std::forward<Args>(args)...);
  }

  template <typename Op, typename ...Args>
  Op create(schir::SourceLocation Loc, Args&& ...args) {
    if (Builder.getBlock() == nullptr) {
      InsertTopLevelCommandOp(Loc);
    } else if (IsLocalDefineAllowed) {
      // An error may occur here, but we always want
      // this function to return a valid operation.
      // Just continue until the error is realized.
      if (!CheckError())
        FinishLocalDefines();
    }
    return createHelper<Op>(Builder, Loc, std::forward<Args>(args)...);
  }

  template <typename Op, typename ...Args>
  Op createTopLevel(schir::SourceLocation Loc, Args&& ...args) {
    assert(!LibraryEnvProc &&
        "Should not be in a library spec context");
    return createHelper<Op>(ModuleBuilder, Loc, std::forward<Args>(args)...);
  }

  static mlir::Value toValue(schir::Value V) {
    if (mlir::Operation* Op = V.get<::schir::ValueSumType::Operation>()) {
      return VisitOperation(Op);
    }
    if (schir::ContArg* Arg = V.get<::schir::ValueSumType::ContArg>()) {
      return VisitContArg(Arg);
    }
    return mlir::Value();
  }

  static schir::Value fromValue(mlir::Value V) {
    if (!V) return schir::Value();
    if (auto OpResult = mlir::dyn_cast<mlir::OpResult>(V)) {
      return schir::Value(OpResult.getOwner());
    }
    if (auto BlockArg = mlir::dyn_cast<mlir::BlockArgument>(V)) {
      // FIXME Do we even get here? Is ContArg ever needed?
      mlir::Block* B = BlockArg.getOwner();
      return schir::Value(reinterpret_cast<schir::ContArg*>(B));
    }
    llvm_unreachable("invalid mlir value kind");
  }

  mlir::Value createCall(schir::SourceLocation Loc, mlir::Value Fn,
                         llvm::MutableArrayRef<mlir::Value> Args);
  mlir::Value createError(schir::SourceLocation Loc,
                          llvm::MutableArrayRef<mlir::Value> Args);
  mlir::Value createOpGen(SourceLocation Loc, mlir::Value Input);
  mlir::Value createBody(SourceLocation Loc, Value Body);
  mlir::Value createSequence(SourceLocation Loc, Value Body);
  mlir::Value createSyntaxSpec(Pair* SyntaxSpec, Value OrigCall);
  mlir::Value createSyntaxRules(SourceLocation Loc, Value Keyword,
                                Value Ellipsis, Value KeywordList,
                                Value SyntaxDef);
  mlir::Value createIf(SourceLocation Loc, Value Cond, Value Then,
                            Value Else);
  mlir::Value createContinuation(mlir::Operation* CallOp);

  enum class RestParamKind {
    None = 0,
    List,
    ValueRefs,
  };

  mlir::FunctionType createFunctionType(unsigned Arity,
                                        RestParamKind RPK);
  schir::FuncOp createFunction(SourceLocation Loc,
                               mlir::FunctionType FT,
                               llvm::StringRef MangledName = {});
  schir::FuncOp createSyntaxFunction(SourceLocation Loc);
  schir::FuncOp createSyntaxFunction(SourceLocation Loc, schir::Value Proc);
  mlir::Value createLambda(Value Formals, Value Body,
                           SourceLocation Loc,
                           llvm::StringRef Name = {});
  mlir::Value createLambda(schir::SourceLocation Loc,
                           llvm::StringRef Name,
                           llvm::MutableArrayRef<mlir::Value> Captures);
  std::pair<schir::FuncOp, EnvFrame*>
  createLambdaFunction(Value Formals, SourceLocation Loc,
                       llvm::StringRef Name = {});
  mlir::Value createLambdaBody(schir::SourceLocation Loc,
                               schir::Value Body,
                               schir::FuncOp, EnvFrame* EF);
  mlir::Value createCaseLambda(Pair* FullExpr);

  mlir::Value createGlobal(SourceLocation Loc, llvm::StringRef MangledName);
  mlir::Value createBinding(Binding *B, mlir::Value Init);
  mlir::Value createDefine(Value Id, Value Args, Value OrigCall);
  std::tuple<EnvEntry, Symbol*, String*> createTopLevelBindingInfo(Value Id);
  mlir::Value createTopLevelDefine(Value Id, Value Args, Value OrigCall);
  mlir::Value createExternalBinding(Value Id, Value ExtSymbol);
  mlir::Value createUndefined();
  mlir::Value createSet(SourceLocation Loc, Value LHS, Value RHS);

  mlir::Value createEqual(Value V1, Value V2); // equal? for pattern matching

  schir::LiteralOp createLiteral(schir::SourceLocation Loc, Value V) {
    return create<LiteralOp>(Loc, V);
  }
  schir::LiteralOp createLiteral(Value V) {
    return createLiteral(V.getSourceLocation(), V);
  }

  void createLoadModule(SourceLocation Loc, Symbol* MangledName);

  mlir::Value SetError(schir::Error* NewErr) {
    assert((!Err || Value(NewErr) == Err) && "no squashing errors");
    Err = NewErr;
    Context.setLoc(Err.getSourceLocation());
    if (RunSyncDepth == 0)
      Context.Raise(Err);
    else
      Context.Yield(Err);
    return Error();
  }

  template <typename T>
  mlir::Value SetError(SourceLocation Loc, T Str,
                       llvm::ArrayRef<Value> IrrArgs = {}) {
    schir::Error* E = Context.CreateError(Loc, Str, Context.CreateList(IrrArgs));
    return SetError(E);
  }

  template <typename T>
  mlir::Value SetError(T Str, llvm::ArrayRef<Value> IrrArgs = {}) {
    schir::SourceLocation Loc;
    for (Value Irr : llvm::reverse(IrrArgs))
      Loc = Loc.isValid() ? Loc : Irr.getSourceLocation();
    return SetError(Loc, Str, IrrArgs);
  }

  template <typename T>
  mlir::Value SetError(T Str, Value V) {
    return SetError(Str, llvm::ArrayRef<Value>(V));
  }

  mlir::Value Error() {
    return createUndefined();
  }

  mlir::Value LocalizeValue(mlir::Value V, schir::Value B = nullptr);
  mlir::Value LocalizeValueOrBinding(mlir::Value V, schir::Value B = nullptr);
  mlir::Value VisitEnvEntry(schir::SourceLocation Loc, EnvEntry Entry);

  mlir::Value LocalizeRec(schir::Value B,
                          mlir::Value V,
                          mlir::Operation* Owner,
                          LambdaScopeIterator Itr);

  mlir::Value VisitDefineArgs(Value Args);

  mlir::Value VisitValue(Value V) {
    return createLiteral(V);
  }

  mlir::Value VisitUndefined(Undefined U) {
    return createUndefined();
  }

  // VisitOperation and VisitContArg are both idempotent
  // so they are declared static for reuse in the static
  // value conversion functions.
  static mlir::Value VisitOperation(mlir::Operation* Op) {
    assert(Op->getNumResults() == 1 && "expecting a single value");
    return Op->getResult(0);
  }
  static mlir::Value VisitContArg(schir::ContArg* Arg) {
    mlir::Block* B = reinterpret_cast<mlir::Block*>(Arg);
    // There should be two args including the closure object.
    assert(B->getNumArguments() == 2 && "expecting a single value");
    return B->getArgument(1);
  }

  mlir::Value VisitBuiltin(Builtin* B) {
    return SetError("internal pointer cannot be made external", B);
  }

  mlir::Value VisitExternName(ExternName* EN);
  mlir::Value VisitSyntaxClosure(SyntaxClosure* SC);
  mlir::Value VisitSymbol(Symbol* S, Value ClosedEnv = nullptr);
  mlir::Value VisitBinding(Binding* B);

  mlir::Value VisitPair(Pair* P);
  mlir::Value VisitVector(Vector* V);

  mlir::Value Lookup(schir::Value V) {
    return BindingTable.lookup(V);
  }

  mlir::Operation* LookupSymbol(llvm::StringRef MangledName);

  schir::EnvEntry LookupEnv(schir::Value Id, schir::Value ClosedEnv = nullptr);

  // Try to get a suitable type for a known value.
  static mlir::Type getValueType(mlir::MLIRContext* MC, schir::Value V) {
    switch (V.getKind()) {
    case ValueKind::Binding:
      return SchirBindingType::get(MC);
    case ValueKind::Syntax:
      return SchirSyntaxType::get(MC);
    case ValueKind::Undefined:
      return SchirUndefinedType::get(MC);
    case ValueKind::Lambda:
    case ValueKind::Builtin:
      return SchirProcedureType::get(MC);
    case ValueKind::Pair:
      return SchirPairType::get(MC);
    default:
      return SchirValueType::get(MC);
    }
  }

  // Attempt to validate the type of a schir::Value.
  static bool validateType(mlir::Type T, schir::Value H) {
    // The opaquest type
    return
      isa<SchirUnknownType>(T) ||
      // The types we just do not check
      isa<SchirValueRefsType>(T) ||
      // The types that cannot be !schir.value
      // Binding => !schir.binding
      ((!isa<schir::Binding>(H) || isa<SchirBindingType>(T)) &&
      // Syntax => !schir.syntax
      (!isa<SchirSyntaxType>(T) || isa<schir::Syntax>(H)) &&
      // The types that can be !schir.value but are more strict
      // !schir.procedure => Lambda | Builtin
      (!isa<SchirProcedureType>(T) || isa<schir::Lambda, schir::Builtin>(H)) &&
      // !schir.undefined => Undefined
      (!isa<SchirUndefinedType>(T) || isa<schir::Undefined>(H)) &&
      // !schir.pair => Pair
      (!isa<SchirPairType>(T) || isa<schir::Pair>(H)) &&
      // !schir.empty => Empty
      (!isa<SchirEmptyType>(T) || isa<schir::Empty>(H)) &&
      // !schir.rest => Pair | Empty
      (!isa<SchirRestType>(T) || isa<schir::Pair, schir::Empty>(H)) &&
      // !schir.vector => Vector
      (!isa<SchirVectorType>(T) || isa<schir::Vector>(H)));
  }

private:
  mlir::Value CallSyntax(Value Operator, Pair* P);
  mlir::Value HandleCall(Pair* P, schir::EnvEntry FnEnvEntry);
};

}

#endif
