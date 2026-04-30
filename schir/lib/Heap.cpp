//===--- CopyCollector.cpp - SchirScheme GC Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations for schir scheme CopyCollector.
//
//===----------------------------------------------------------------------===//

#include "schir/Context.h"
#include "schir/Heap.h"
#include "schir/OpGen.h"
#include "schir/Value.h"
#include "schir/ValueVisitor.h"
#include "llvm/Support/Allocator.h"
#include <algorithm>

namespace schir {
// CopyCollector
//  - Visit a Root node, copy the value to the current heap
//    and overwrite the schir::Value with its replacement.
class CopyCollector : private ValueVisitor<CopyCollector, schir::Value> {
  using AllocatorTy = llvm::BumpPtrAllocator;
  using Base = ValueVisitor<CopyCollector, schir::Value>;
  friend class ValueVisitor<CopyCollector, schir::Value>;

  AllocatorTy& NewHeap;
  AllocatorTy& OldHeap;
  llvm::AllocatorBase<AllocatorTy>& getAllocator() { return NewHeap; }
  // Track objects that are visited but not themselves collected.
  std::vector<void*> VisitedSpecials;

  // Return false if Special was already visited.
  bool markSpecialVisited(void* Ptr) {
    auto Itr = std::find(VisitedSpecials.begin(), VisitedSpecials.end(), Ptr);
    if (Itr != VisitedSpecials.end())
      return false;

    VisitedSpecials.push_back(Ptr);
    return true;
  }

  // Handle all of the types that are embedded in the pointer,
  // and, therefore do not require a heap allocation.
  // FIXME These are unreachable.
  schir::Value VisitInt(schir::Int V)               { return V; }
  schir::Value VisitBool(schir::Bool V)             { return V; }
  schir::Value VisitChar(schir::Char V)             { return V; }
  schir::Value VisitEmpty(schir::Empty V)           { return V; }
  schir::Value VisitOperation(schir::Operation* V)  { return V; }
  schir::Value VisitContArg(schir::ContArg* V)      { return V; }

  schir::Value VisitUndefined(schir::Undefined V)   {
    schir::Value NewTracer = Visit(V.getTracer());
    return Value(schir::Undefined(NewTracer));
  }

  // String
  schir::String* VisitString(schir::String* String) {
    llvm::StringRef StringRef = String->getView();
    unsigned MemSize = String::sizeToAlloc(StringRef.size());
    void* Mem = schir::allocate(getAllocator(), MemSize, alignof(schir::String));
    return new (Mem) schir::String(StringRef);
  }


  schir::EnvEntry VisitEnvEntry(schir::EnvEntry const& EnvEntry) {
    schir::Value Value = Visit(EnvEntry.Value);
    schir::String* MangledName
      = cast<String>(Visit(EnvEntry.MangledName));
    return schir::EnvEntry{Value, MangledName};
  }

  schir::Value VisitBigInt(schir::BigInt* B) {
    return new (NewHeap) schir::BigInt(B->Val);
  }

  schir::Value VisitBuiltin(schir::Builtin* B) {
    return new (NewHeap) schir::Builtin(B->Fn);
  }

  // Binding
  schir::Binding* VisitBinding(schir::Binding* Binding) {
    return new (NewHeap) schir::Binding(
      Visit(Binding->getIdentifier()),
      Visit(Binding->getValue()));
  }

  // BuiltinSyntax
  schir::Value VisitBuiltinSyntax(schir::BuiltinSyntax* BuiltinSyntax) {
    return new (NewHeap) schir::BuiltinSyntax(*BuiltinSyntax);
  }

  // ByteVector
  schir::Value VisitByteVector(schir::ByteVector* ByteVector) {
    schir::String* NewString = cast<String>(Visit(ByteVector->getString()));
    return new (NewHeap) schir::ByteVector(NewString);
  }

  // EnvFrame
  schir::Value VisitEnvFrame(schir::EnvFrame* EnvFrame) {
    llvm::ArrayRef<schir::Binding*> Bindings = EnvFrame->getBindings();
    bool IsLambdaScope = EnvFrame->isLambdaScope();
    unsigned MemSize = EnvFrame::sizeToAlloc(Bindings.size());

    void* Mem = NewHeap.Allocate(MemSize, alignof(schir::EnvFrame));

    schir::EnvFrame* NewE = new (Mem) schir::EnvFrame(Bindings.size(),
                                                      IsLambdaScope);
    auto NewBindings = NewE->getBindings();
    for (unsigned i = 0; i < Bindings.size(); i++) {
      NewBindings[i] = cast<schir::Binding>(Visit(Bindings[i]));
    }
    return NewE;
  }

  // Error
  schir::Value VisitError(schir::Error* Error) {
    return new (NewHeap) schir::Error(
      static_cast<ValueWithSource*>(Error)->getSourceLocation(),
      Visit(Error->getMessage()),
      Visit(Error->getIrritants()));
  }

  // Exception
  schir::Value VisitException(schir::Exception* Exception) {
    return new (NewHeap) schir::Exception(Visit(Exception->Val));
  }

  // ExternName
  schir::Value VisitExternName(schir::ExternName* ExternName) {
    return new (NewHeap) schir::ExternName(
      cast<String>(Visit(ExternName->getName())),
      static_cast<ValueWithSource*>(ExternName)->getSourceLocation()
      );
  }

  // Float
  schir::Value VisitFloat(schir::Float* Float) {
    return new (NewHeap) schir::Float(Float->getVal());
  }

  // ForwardRef
  schir::Value VisitForwardRef(schir::ForwardRef* ForwardRef) {
    // This is a wrapper for an already copied value.
    return ForwardRef->Val;
  }

  // ImportSet
  schir::Value VisitImportSet(schir::ImportSet* ImportSet) {
    return new (NewHeap) schir::ImportSet(
      ImportSet->getImportKind(),
      cast_or_null<schir::ImportSet>(Visit(ImportSet->getParent())),
      Visit(ImportSet->getSpecifier()));
  }

  // Lambda
  schir::Value VisitLambda(schir::Lambda* Old) {
    OpaqueFn FnData = Old->getFnData();
    llvm::ArrayRef<schir::Value> Captures = Old->getCaptures();
    void* Mem = schir::Lambda::allocate(getAllocator(), FnData, Captures);
    return new (Mem) Lambda(FnData, Captures);
  }

  // Pair
  schir::Value VisitPair(schir::Pair* Pair) {
    schir::Value Car = Visit(Pair->Car);
    schir::Pair* Bottom = isa<schir::PairWithSource>(schir::Value(Pair))
        ? new (NewHeap) schir::PairWithSource(Car, schir::Empty(),
                                              Pair->getSourceLocation())
        : new (NewHeap) schir::Pair(Car, schir::Empty());
    schir::Value OldCdr = Pair->Cdr;

    schir::Pair* Top = Bottom;
    while (schir::Pair* P = dyn_cast<schir::Pair>(OldCdr)) {
      schir::Value Car = Visit(P->Car);
      Top->Cdr = isa<schir::PairWithSource>(schir::Value(P))
          ? new (NewHeap) schir::PairWithSource(Car, schir::Empty(),
                                                P->getSourceLocation())
          : new (NewHeap) schir::Pair(Car, schir::Empty());
      Top = cast<schir::Pair>(Top->Cdr);
      OldCdr = P->Cdr;
    }

    // Handle improper list. (ie OldCdr was not a Pair or Empty)
    if (!isa<schir::Empty>(OldCdr)) {
      Top->Cdr = Visit(OldCdr);
      // Do not update Top since we are done.
    }

    return Bottom;
  }

  // PairWithSource
  schir::Value VisitPairWithSource(schir::PairWithSource* PairWithSource) {
    return VisitPair(PairWithSource);
  }

  // Quote
  schir::Value VisitQuote(schir::Quote* Quote) {
    return new (NewHeap) schir::Quote(Visit(Quote->Val));
  }

  // SourceValue
  schir::Value VisitSourceValue(schir::SourceValue* SourceValue) {
    return new (NewHeap) schir::SourceValue(*SourceValue);
  }

  // Symbol
  schir::Value VisitSymbol(schir::Symbol* Symbol) {
    return new (NewHeap) schir::Symbol(cast<String>(Visit(Symbol->getString())),
      static_cast<ValueWithSource*>(Symbol)->getSourceLocation());
  }

  // Syntax
  schir::Value VisitSyntax(schir::Syntax* Syntax) {
    OpaqueFn FnData = Syntax->getFnData();
    void* Mem = Syntax::allocate(getAllocator(), FnData);
    return new (Mem) schir::Syntax(FnData);
  }

  // SyntaxClosure
  schir::Value VisitSyntaxClosure(schir::SyntaxClosure* SyntaxClosure) {
    return new (NewHeap) schir::SyntaxClosure(
        SyntaxClosure->getSourceLocation(),
        Visit(SyntaxClosure->Env),
        Visit(SyntaxClosure->Node));
  }

  // Vector
  schir::Value VisitVector(schir::Vector* Vector) {
    schir::ArrayRef<schir::Value> Xs = Vector->getElements();
    return new (NewHeap, Xs) schir::Vector(Xs);
  }

  schir::Value VisitAny(schir::Any* Any) {
    void const* TypeId = Any->TypeId;
    llvm::StringRef ObjData = Any->getObjData();
    void* Mem = Any::allocate(getAllocator(), ObjData);
    return new (Mem) schir::Any(TypeId, ObjData);
  }

  template <typename ...Args>
  schir::Value Visit(schir::Value OldVal) {
    // Handle the ValueSumTypes that alias ValueBase
    if (isa<Undefined>(OldVal))
      return VisitUndefined(cast<Undefined>(OldVal));

    void* ValueBase = OldVal.get<ValueSumType::ValueBase>();
    if (ValueBase == nullptr)
      return OldVal;

    // This runs two comparisons to see if the pointer
    // is within the bounds of OldHeap.
    if (!OldHeap.identifyObject(ValueBase).has_value())
      return OldVal;
    // NOTE: The above check allows us to have special allocators
    //       for identifiers and other "static" objects that we
    //       can avoid unnecessary copying.

    schir::Value NewVal = Base::Visit(OldVal);
    // Overwrite the OldVal with a ForwardRef.
    assert(NewVal && "expecting valid value");
    new (ValueBase) ForwardRef(NewVal);

    return NewVal;
  }

public:
  CopyCollector(AllocatorTy& New, AllocatorTy& Old)
    : NewHeap(New),
      OldHeap(Old)
  { }

  void VisitRootNode(schir::Value& Val) {
    if (!Val) return;
    Val = Visit(Val);
  }

  // Module
  schir::Value VisitModule(schir::Module* Module) {
    if (!markSpecialVisited(Module))
      return Module;

    // The module itself is not garbage collected,
    // but it has contained objects that are.
    if (Module->Cleanup)
      Module->Cleanup = cast_or_null<Lambda>(Visit(Module->Cleanup));
    for (auto& DensePair : Module->Map) {
      DensePair.getFirst() = cast<String>(Visit(DensePair.getFirst()));
      DensePair.getSecond() = cast<String>(Visit(DensePair.getSecond()));
    }
    return Module;
  }

  // Environment
  schir::Value VisitEnvironment(schir::Environment* Env) {
    // Check if previously visited.
    if (!markSpecialVisited(Env))
      return Env;

    // Environment itself is not garbage collected, but it
    // contains objects that are.
    if (schir::OpGen* OpGen = Env->OpGen.get()) {
      if (OpGen->TopLevelHandler)
        OpGen->TopLevelHandler = Visit(OpGen->TopLevelHandler);
      if (OpGen->LibraryEnvProc)
        OpGen->LibraryEnvProc = cast<Binding>(Visit(OpGen->LibraryEnvProc));
      // Note that the BindingTable has no unique ownership of the Bindings
      // as they are all pushed to the Environment.
    }

    for (auto& DensePair : Env->EnvMap) {
      DensePair.getFirst() = cast<String>(Visit(DensePair.getFirst()));
      DensePair.getSecond() = cast<String>(Visit(DensePair.getSecond()));
    }

    VisitedSpecials.push_back(Env);
    return Env;
  }
};

void Context::CollectGarbage() {
  // FIXME The continuation stack and escape procedures
  //       are saved as opaque string objects.
  //       Wrap these in some kind of  "ContStack" value
  //       and make a visitor for their captures.
  llvm::errs() << "NOT COLLECTING GARBAGE: " << getBytesAllocated() << "\n";
  MaxHint *= 2;
  return;

  // Create NewHeap
  Heap::AllocatorTy NewHeap;
  CopyCollector GC(NewHeap, this->TrashHeap);

  // Note that Environments are captured in Lambdas.

  // Visit all Context.Modules
  for (auto& StringMapEntry : this->Modules) {
    GC.VisitModule(StringMapEntry.second.get());
  }

  GC.VisitRootNode(EnvStack);

  // DefaultEnv is referenced globally.
  GC.VisitEnvironment(DefaultEnv.get());

  // KnownAddresses
  for (auto& DensePair : KnownAddresses) {
    // The String* key is allocated with IdTable.
    GC.VisitRootNode(DensePair.second);
  }

  // LookupTable
  for (auto& [_, V] : LookupTable)
    GC.VisitRootNode(V);

  GC.VisitRootNode(Err);
  GC.VisitRootNode(ExceptionHandlers);

  // Visit ModuleOp (for contained LiteralOps, MatchOps)
  if (ModuleOp) {
    auto WalkerFn = [](mlir::Operation* Op) {
      schir::SchirValueAttr ValAttr;
      if (auto LiteralOp = dyn_cast<schir::LiteralOp>(Op))
        ValAttr = LiteralOp.getInputAttr();
      else if (auto MatchOp = dyn_cast<schir::MatchOp>(Op))
        ValAttr = MatchOp.getValAttr();

      // Just clear the value and the attr will rebuild it
      // from its expression when and if it is requested.
      if (ValAttr)
        ValAttr.getCachedValue() = Value();
    };
    ModuleOp->walk(WalkerFn);

  }
  ReplaceHeap(std::move(NewHeap));
}

String* IdTable::CreateIdTableEntry(llvm::StringRef Str) {
  String*& Entry = IdTableMap[Str];
  if (Entry)
    return Entry;

  unsigned MemSize = String::sizeToAlloc(Str.size());
  void* Mem = IdHeap.Allocate(MemSize, alignof(String));

  Entry = new (Mem) String(Str);
  return Entry;
}
String* IdTable::CreateIdTableEntry(llvm::StringRef Prefix,
                                    llvm::StringRef Str) {
  Buffer.clear();
  Buffer += Prefix;
  Buffer += Str;
  return CreateIdTableEntry(Buffer);
}

}  // namespace schir

