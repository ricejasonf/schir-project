//===---- OpGen.cpp - Classes for generating MLIR Operations ----*- C++ -*-===//
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

#include "PatternTemplate.h"
#include "schir/Builtins.h"
#include "schir/Context.h"
#include "schir/Dialect.h"
#include "schir/Mangle.h"
#include "schir/OpGen.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SaveAndRestore.h"
#include <memory>

using namespace schir;

static ContextLocal SyntaxStack;

// Set the syntax stack until the result is destroyed.
static auto setSyntaxStack(schir::Context& C, Value Stack) {
  Binding* SSB = SyntaxStack.getBinding(C);
  Value PrevStack = SSB->getValue();
  SSB->setValue(Stack);
  return llvm::scope_exit([SSB, PrevStack] {
      SSB->setValue(PrevStack);
    });
}

// Push '(Operator . P) to the syntax stack until the result is destroyed.
static auto pushSyntaxStack(schir::Context& C, Value Operator, Pair* P) {
  Value Stack = SyntaxStack.getBinding(C)->getValue();
  return setSyntaxStack(C, C.CreatePair(C.CreatePair(Operator, P), Stack));
}

// LambdaScope - RAII object that pushes an operation that is
//               FunctionLike to the scope stack along with a
//               BindingScope where we can insert stack local
//               values resulting from operations that load from
//               a global or variable captured by a closure.
//
//               Note that BindingScope is still used for
//               non-isolated scopes (e.g. `let` syntax).
struct OpGen::LambdaScope {
  OpGen& O;
  LambdaScopeNode& Node;

  LambdaScope(OpGen& O, mlir::Operation* Op)
    : O(O),
      Node((O.LambdaScopes.emplace_back(Op, /*CallOp*/nullptr,
                                        O.BindingTable),
            O.LambdaScopes.back()))
  { }

  ~LambdaScope() {
    // pop all intermediate continuation scopes
    // and then our own lambda scope
    while (O.LambdaScopes.size() > 0) {
      mlir::Operation* CurOp = O.LambdaScopes.back().Op;
      if (CurOp == Node.Op) {
        O.LambdaScopes.pop_back();
        return;
      } else {
        O.PopContinuationScope();
      }
    }
    llvm_unreachable("scope should be on stack");
  }
};

OpGen::OpGen(schir::Context& C, schir::Symbol* ModulePrefix)
  : Context(C),
    ModuleBuilder(C.MLIRContext.get()),
    Builder(C.MLIRContext.get()),
    BindingTable(),
    ModulePrefix(ModulePrefix)
{
  mlir::Location Loc = Builder.getUnknownLoc();
  mlir::OpBuilder ImportsBuilder(C.MLIRContext.get());
  mlir::ModuleOp TopModule;
  if (!C.OpGen) {
    assert(!C.ModuleOp && "There should be only one top level module");
    C.MLIRContext->loadDialect<schir::SchirDialect, mlir::func::FuncDialect>();
    // Create the module that contains the main module and import modules
    TopModule = mlir::ModuleOp::create(Builder, Loc);
    C.ModuleOp = TopModule;
    // The first child ModuleOp is the main module. All subsequent
    // modules are imports.
  } else {
    TopModule = cast<mlir::ModuleOp>(C.ModuleOp);
  }
  ImportsBuilder.setInsertionPointToEnd(TopModule.getBody());
  ModuleOp = mlir::ModuleOp::create(ImportsBuilder, Loc,
                                    getModulePrefix());

  ModuleBuilder.setInsertionPointToStart(
      cast<mlir::ModuleOp>(ModuleOp).getBody());
  LambdaScopes.emplace_back(ModuleOp, /*CallOp=*/nullptr, BindingTable);
}

OpGen::~OpGen() {
  assert((LambdaScopes.size() == 1 || CheckError())
      && "LambdaScopes should be unwound");
  // Unwind if there was an error.
  while (LambdaScopes.size() > 1)
    LambdaScopes.pop_back();
}

std::string OpGen::mangleModule(schir::Value NameSpec) {
  return createMangler().mangleModule(schir::ManglePrefix, NameSpec);
}

std::string OpGen::mangleFunctionName(llvm::StringRef Name) {
  if (Name.empty())
    return mangleAnonymousId();
  return createMangler().mangleFunction(getModulePrefix(), Name);
}

// Mangle a unique name in the current module.
std::string OpGen::mangleAnonymousId() {
  return createMangler().mangleAnonymousId(getModulePrefix(),
                                           LambdaNameCount++);
}

std::string OpGen::mangleVariable(schir::Value Name) {
  return createMangler().mangleVariable(getModulePrefix(), Name);
}

mlir::ModuleOp OpGen::getModuleOp() {
  return cast<mlir::ModuleOp>(ModuleOp);
}

void OpGen::createLoadModule(schir::SourceLocation Loc,
                             schir::Symbol* MangledName) {
  TopLevelOp = createHelper<schir::LoadModuleOp>(ModuleBuilder, Loc,
                                    MangledName->getStringRef());
}

mlir::Value OpGen::GetSingleResultOrBinding(schir::Value V) {
  // Ensure we are no longer top level.
  if (!TopLevelOp)
    InsertTopLevelCommandOp(Context.getLoc());

  TailPosScope TPS(*this);
  IsTailPos = false;
  mlir::Value Result = Visit(V);

  if (CheckError())
    return Error();

  // mlir::Value() is only returned for expressions in tail position
  //               or syntax that should not be used outside proper
  //               context such as `import`, `define-syntax`, etc.
  if (!Result)
      return SetError("expecting expression", V);

  // TODO Use MatchArgsOp to destructure ValueRefs to a single
  //      argument (or raise an error.)
  // Take the first continuation arg.
  if (Result && isa<SchirValueRefsType>(Result.getType()))
    Result = create<LoadRefOp>(schir::SourceLocation(), Result, /*Index*/0);

  return LocalizeValueOrBinding(Result);
}

mlir::Value OpGen::GetSingleResult(schir::Value V) {
  mlir::Value Result = GetSingleResultOrBinding(V);
  return UnwrapBinding(Result);
}

// Insert a check to assert the type of a value.
mlir::Value OpGen::CheckType(schir::SourceLocation Loc, mlir::Value V,
                             mlir::Type Type) {
  return create<MatchTypeOp>(Loc, Type, V);
}

// WithLibraryEnv - Call a thunk within the library environment
//                  for <library spec>. (ie begin, import, export)
void OpGen::WithLibraryEnv(Value Thunk) {
  if (!LibraryEnvProc) {
    SetError("not in a library context");
    return;
  }
  assert(!isa<Undefined>(LibraryEnvProc->getValue()) &&
      "LibraryEnvProc must be initialized");
  Context.CallCC(Context.CreateLambda([](schir::Context& C, ValueRefs Args) {
    Value CC = Args[0];
    Value Thunk = C.getCapture(0);
    Binding* LibraryEnvProc = cast<Binding>(C.getCapture(1));

    Value NewThunk = C.CreateLambda([](schir::Context& C, ValueRefs) {
      Value Thunk = C.getCapture(0);
      Value CC = C.getCapture(1);
      C.PushCont(CC);  // Resume the current continuation.
      C.ApplyThunk(Thunk);
    }, CaptureList{Thunk, CC});

    C.Apply(LibraryEnvProc->getValue(), NewThunk);
  }, CaptureList{Thunk, LibraryEnvProc}));
}

void OpGen::VisitLibrary(schir::SourceLocation Loc,
                         schir::Symbol* MangledName,
                         Value LibraryDecls) {
  assert(isTopLevel() && "library should be top level");
  // LibraryEnvProc - Capture and later set to the escape procedure
  //                  that is used to process top level exprs for the
  //                  library which must be with a (begin ...) syntax.
  assert(!LibraryEnvProc && "should not already be in library context");
  LibraryEnvProc = Context.CreateBinding(Undefined());
  Value HandleLibraryDecls = Context.CreateBinding(Int(5));
  LibraryDecls = Context.CreateBinding(LibraryDecls);

  // PushCont the creation of LibraryEnvProc
  Context.PushCont([](schir::Context& C, ValueRefs) {
    Binding* HandleLibraryDecls = cast<Binding>(C.getCapture(0));
    Value LibraryEnvProc = C.getCapture(1);
    Symbol* MangledName = cast<Symbol>(C.getCapture(2));

    if (isa<Empty>(HandleLibraryDecls->getValue())) {
      C.Cont();
      return;
    }

    Value Thunk = C.CreateLambda([](schir::Context& C, ValueRefs) {
      Binding* HandleLibraryDecls = cast<Binding>(C.getCapture(0));
      Value LibraryEnvProc = C.getCapture(1);

      // PushCont the initial calling of HandleLibraryDecls
      C.PushCont(HandleLibraryDecls->getValue());

      C.SaveEscapeProc(LibraryEnvProc, [](schir::Context& C, ValueRefs Args) {
        // Handle the sequence from `begin` from within the lib environment.
        Binding* HandleLibraryDecls = cast<Binding>(C.getCapture(0));
        Binding* LibraryEnvProc = cast<Binding>(C.getCapture(1));
        if (isa<Lambda>(HandleLibraryDecls->getValue())) {
          C.PushCont(HandleLibraryDecls->getValue());
          C.PushCont([](schir::Context& C, ValueRefs Args) {
            // Args[0] is Thunk from OpGen::WithLibraryEnv(Value Thunk)
            C.ApplyThunk(Args[0]);
          });
          C.Cont(Args[0]);
        } else {
          LibraryEnvProc->setValue(Empty());
          C.OpGen->FinishLibrary();
        }
      }, CaptureList{HandleLibraryDecls, LibraryEnvProc});
    }, CaptureList{HandleLibraryDecls, LibraryEnvProc});
    // Taking ownership of EnvPtr
    auto EnvPtr = std::make_unique<schir::Environment>(C, MangledName);
    C.WithEnv(std::move(EnvPtr), Thunk);
  }, CaptureList{HandleLibraryDecls, LibraryEnvProc, MangledName});


  // The library environment isn't used until we are in a sequence (begin).
  // Use the parent environment without allowing operations to be inserted.
  auto LibraryBefore = Context.CreateLambda([](schir::Context& C, ValueRefs) {
    // Store LibraryEnvProc as a member so it is accessible within
    // library specs
    C.OpGen->LibraryEnvProc = cast<Binding>(C.getCapture(0));
    C.Cont();
  }, CaptureList{LibraryEnvProc});
  auto LibraryAfter = Context.CreateLambda([](schir::Context& C, ValueRefs) {
    C.OpGen->LibraryEnvProc = nullptr;
    C.Cont();
  }, CaptureList{});
  auto LibraryThunk = Context.CreateLambda([](schir::Context& C, ValueRefs) {
    Value HandleLibraryDecls = C.getCapture(0);
    Value LibraryDecls       = C.getCapture(1);
    C.SaveEscapeProc(HandleLibraryDecls, [](schir::Context& C, ValueRefs) {
      Binding* HandleLibraryDecls = cast<Binding>(C.getCapture(0));
      Binding* LibraryDecls = cast<Binding>(C.getCapture(1));
      if (isa<Empty>(C.OpGen->LibraryEnvProc->getValue())) {
        C.Cont();
      } else if (Pair* P = dyn_cast<Pair>(LibraryDecls->getValue())) {
        LibraryDecls->setValue(P->Cdr);
        C.PushCont(HandleLibraryDecls->getValue());
        C.OpGen->VisitLibrarySpec(P->Car);
      } else if (isa<Empty>(LibraryDecls->getValue())) {
        // The library is done.
        // Unset stuff so the cleanups will run.
        cast<Binding>(HandleLibraryDecls)->setValue(Empty());
        C.Apply(C.OpGen->LibraryEnvProc->getValue(), Value(Empty()));
        C.PushCont(HandleLibraryDecls->getValue());
      } else {
        C.OpGen->SetError("expected proper list for library declarations",
                          LibraryDecls->getValue());
      }
    }, CaptureList{HandleLibraryDecls, LibraryDecls});
  }, CaptureList{HandleLibraryDecls, LibraryDecls});

  Context.DynamicWind(LibraryBefore,
                      LibraryThunk,
                      LibraryAfter);
}

void OpGen::VisitLibrarySpec(Value LibSpec) {
  // Use PushCont to avoid destruction of this.
  Context.PushCont([](schir::Context& C, ValueRefs) {
    Value LibSpec = C.getCapture(0);
    if (Pair* P = dyn_cast<Pair>(LibSpec)) {
      schir::Value SyntaxOperator;
      if (isIdentifier(P->Car))
        SyntaxOperator = C.GetSyntax(C.OpGen->LookupEnv(P->Car)).Value;
      if (SyntaxOperator) {
        C.OpGen->CallSyntax(SyntaxOperator, P);
      } else {
        C.OpGen->SetError("expecting library spec", LibSpec);
        return;
      }
      if (C.OpGen->CheckError())
        return;
    }

    C.Cont();
  }, CaptureList{LibSpec});
  Context.Cont();
}

void OpGen::FinishLibrary() {
  auto M = cast<mlir::ModuleOp>(this->ModuleOp);

  // Check symbols of export-ids.
  for (mlir::Operation& Op : *M.getBody()) {
    if (auto E = dyn_cast<schir::ExportIdOp>(&Op)) {
      if (E.getSymbolName().empty()) {
        // Try looking up the name.
        EnvEntry Entry = LookupEnv(Context.CreateSymbol(E.getOrigId()));
        if (!Entry.MangledName) {
          this->Context.setLoc(schir::Value::getSourceLocation(E));
          this->Context.RaiseError(
              "export name must reference a defined global value or syntax");
          return;
        }
        E.setSymbolName(Entry.MangledName->getStringRef());
      }
    } else if (auto G = dyn_cast<schir::GlobalOp>(&Op)) {

      if (G.isExternal() && !Mangler::isExternalVariable(schir::ManglePrefix,
                                                         getModulePrefix(),
                                                         G.getSymName())) {
        // Find a LoadGlobalOp referencing this to get a source location.
        schir::SourceLocation Loc;
        M->walk([&](schir::LoadGlobalOp LG) {
            if (LG.getName() == G.getSymName()) {
              Loc = schir::Value::getSourceLocation(LG);
              return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
          });
        this->Context.setLoc(Loc);
        this->Context.RaiseError(
            "variable name is referenced but never defined");
        return;
      }
    }
  }

  Context.Cont();
}

void OpGen::VisitTopLevel(Value V) {
  if (LibraryEnvProc) {
    VisitLibrarySpec(V);
    return;
  }
  TopLevelOp = nullptr;

  // We use a null Builder to denote that we should
  // insert into a lazily created CommandOp by default
  // The insertion point is saved manually (ie not RAII)
  // and then restored in the continuation.
  mlir::OpBuilder::InsertPoint PrevIp = Builder.saveInsertionPoint();
  Builder.clearInsertionPoint();

  Context.PushCont([this, PrevIp](schir::Context& C, ValueRefs) {
    // Instead of the continuation argument we use TopLevelOp
    FinishTopLevelOp();
    // Clear TopLevelOp so an enclosing top level begin does not finish it.
    mlir::Operation* Result = TopLevelOp;
    TopLevelOp = nullptr;
    if (Result && TopLevelHandler) {
      C.Apply(TopLevelHandler, Value(Result));
    } else {
      C.Cont();
    }

    Builder.restoreInsertionPoint(PrevIp);
  });
  Context.PushCont([](schir::Context& C, ValueRefs) {
    schir::Value V = C.getCapture(0);
    schir::OpGen* OpGen = C.OpGen;  // For debug only.
    C.OpGen->Visit(V);
    assert(OpGen == C.OpGen && "OpGen should not unwind itself.");
    if (C.OpGen->CheckError())
      return;
    C.Cont();
  }, CaptureList{V});
  Context.Cont();
}

void OpGen::FinishTopLevelOp() {
  assert((TopLevelOp == nullptr ||
          isa<CommandOp, GlobalOp, GlobalBindingOp,
              GlobalSyntaxOp, LoadModuleOp>(TopLevelOp)) &&
      "expecting top level operation");

  if (schir::CommandOp CommandOp =
        dyn_cast_or_null<schir::CommandOp>(TopLevelOp)) {
    // Ensure current function body (if any) has a terminator.
    if (auto F = dyn_cast_or_null<FuncOp>(LambdaScopes.back().Op)) {
      mlir::Block& Block = F.getBody().back();
      if (!Block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        mlir::OpBuilder::InsertionGuard IG(Builder);
        Builder.setInsertionPointToEnd(&Block);
        create<ContOp>(schir::SourceLocation(), createUndefined());
      }
    }

    // Ensure the CommandOp body has a terminator.
    mlir::Block& Block = CommandOp.getBody().front();
    assert(!Block.empty() && "command op must have body");
    if (!Block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
      mlir::OpBuilder::InsertionGuard IG(Builder);
      Builder.setInsertionPointToEnd(&Block);
      create<ContOp>(schir::SourceLocation(), createUndefined());
    }
  } else if (schir::GlobalOp GlobalOp =
              dyn_cast_or_null<schir::GlobalOp>(TopLevelOp)) {
    // Wrap the result in a binding.
    mlir::Block* Block = nullptr;
    if (auto F = dyn_cast_or_null<FuncOp>(LambdaScopes.back().Op))
      Block = &F.getBody().back();
    else
      Block = &GlobalOp.getInitializer().back();
    mlir::Operation* TermOp = &Block->back();
    if (auto ContOp = dyn_cast<schir::ContOp>(TermOp)) {
      assert(ContOp.getArgs().size() == 1 &&
             isa<SchirBindingType>(ContOp.getArgs().front().getType()));
      mlir::Value BVal = ContOp.getArgs().front();
      mlir::OpBuilder::InsertionGuard IG(Builder);
      Builder.setInsertionPoint(ContOp);
      schir::InitGlobalOp::create(Builder, GlobalOp.getLoc(),
                                  GlobalOp.getSymName(), BVal);
      mlir::Value Undefined = schir::UndefinedOp::create(Builder,
                                                  GlobalOp.getLoc());
      ContOp.getArgsMutable().assign(Undefined);
    } else {
      mlir::Value Results = createContinuation(TermOp);
      // TODO Use MatchArgsOp to destructure ValueRefs to a single
      //      argument (or raise an error.)
      // Take the first continuation arg.
      mlir::Value Result = create<LoadRefOp>(schir::SourceLocation(),
                                             Results, /*Index*/0);
      mlir::Value BVal = createHelper<BindingOp>(Builder,
          schir::SourceLocation(), Result);
      schir::InitGlobalOp::create(Builder, GlobalOp.getLoc(),
                                  GlobalOp.getSymName(), BVal);
      mlir::Value Undefined = schir::UndefinedOp::create(Builder,
                                                  GlobalOp.getLoc());
      schir::ContOp::create(Builder, GlobalOp.getLoc(), Undefined);
    }
  }

  // Pop continuation scopes to the TopLevelOp.
  while (LambdaScopes.size() > 0) {
    if (LambdaScopes.back().Op == TopLevelOp) {
      // Pop the TopLevelOp.
      LambdaScopes.pop_back();
      break;
    } else if (LambdaScopes.back().Op == ModuleOp) {
      break;
    } else {
      PopContinuationScope();
    }
  }
}

void OpGen::VisitTopLevelSequence(Value List) {
  if (LibraryEnvProc) {
    WithLibraryEnv(Context.CreateLambda([](schir::Context& C, ValueRefs) {
      C.OpGen->VisitTopLevelSequence(C.getCapture(0));
    }, CaptureList{List}));
    return;
  }
  if (Pair* P = dyn_cast<Pair>(List)) {
    Context.PushCont([](schir::Context& C, ValueRefs) {
      Value Rest = C.getCapture(0);
      C.OpGen->VisitTopLevelSequence(Rest);
    }, CaptureList{P->Cdr});
    VisitTopLevel(P->Car);
  }
  else if (isa<Empty>(List)) {
    Context.Cont();
  } else {
    SetError("expected proper list in sequence", List);
  }
}

void OpGen::InsertTopLevelCommandOp(SourceLocation Loc) {
  auto CommandOp = createTopLevel<schir::CommandOp>(Loc);
  setTopLevelOp(CommandOp.getOperation());
  mlir::Block& Block = *CommandOp.addEntryBlock();
  // overwrites Builder without reverting it
  Builder.setInsertionPointToStart(&Block);
}

mlir::Value OpGen::createUndefined() {
  if (Builder.getInsertionBlock() == nullptr)
    return mlir::Value();
  return createHelper<UndefinedOp>(Builder, SourceLocation());
}

mlir::FunctionType OpGen::createFunctionType(unsigned Arity,
                                             RestParamKind RPK) {
  mlir::Type ClosureT   = Builder.getType<SchirContextType>();
  mlir::Type ValueT     = Builder.getType<SchirValueType>();

  llvm::SmallVector<mlir::Type, 16> ArgTypes{};
  // push the closure type
  ArgTypes.push_back(ClosureT);
  if (Arity > 0) {
    for (unsigned i = 0; i < Arity - 1; i++) {
      ArgTypes.push_back(ValueT);
    }

    mlir::Type LastParamT;
    switch (RPK) {
    case RestParamKind::None:
      LastParamT = ValueT;
      break;
    case RestParamKind::List:
      LastParamT = Builder.getType<SchirRestType>();
      break;
    case RestParamKind::ValueRefs:
      LastParamT = Builder.getType<SchirValueRefsType>();
      break;
    }
    ArgTypes.push_back(LastParamT);
  }

  return Builder.getFunctionType(ArgTypes, /*ResultTypes*/{});
}

schir::FuncOp OpGen::createFunction(SourceLocation Loc,
                                    mlir::FunctionType FT,
                                    llvm::StringRef MangledName) {
  std::string MangledNameStr;
  if (MangledName.empty()) {
    MangledNameStr = mangleFunctionName(llvm::StringRef());
    MangledName = MangledNameStr;
  }
  assert(!MangledName.empty() && "mangling should never fail");
  // Insert the FuncOp such that it precedes the current top level op.
  return createHelper<schir::FuncOp>(ModuleBuilder, Loc, MangledName, FT);
}

mlir::Value OpGen::createLambda(Value Formals, Value Body,
                                SourceLocation Loc,
                                llvm::StringRef Name) {
  auto [FuncOp, EnvFrame] = createLambdaFunction(Formals, Loc, Name);
  if (!FuncOp)
    return Error();
  mlir::FunctionType FT = FuncOp.getFunctionType();

  if (isa<SchirRestType>(FT.getInputs().back())) {
    // Create a wrapper function that binds the arguments from ValueRefs.
    mlir::FunctionType WFT = createFunctionType(/*Arity=*/1,
                                                RestParamKind::ValueRefs);
    schir::FuncOp WFuncOp = createFunction(Loc, WFT);
    llvm::SmallVector<mlir::Value, 8> Captures;
    {
      // Start new scope in tail pos
      TailPosScope TPS(*this);
      IsTailPos = true;
      LambdaScope LS(*this, WFuncOp);
      mlir::OpBuilder::InsertionGuard IG(Builder);
      mlir::Block& Block = *WFuncOp.addEntryBlock();
      Builder.setInsertionPointToStart(&Block);
      auto MA = create<MatchArgsOp>(Loc, FT, Block.getArgument(1));
      mlir::Value InnerLambda = createLambdaBody(Loc, Body, FuncOp, EnvFrame);
      // Call the inner lambda with the results of the destructured args.
      create<ApplyOp>(Loc, InnerLambda, MA.getResults());
      Captures = std::move(LS.Node.Captures);
    }
    return createLambda(Loc, WFuncOp.getSymName(), Captures);
  } else {
    return createLambdaBody(Loc, Body, FuncOp, EnvFrame);
  }
}
mlir::Value OpGen::createLambda(schir::SourceLocation Loc,
                                llvm::StringRef Name,
                                llvm::MutableArrayRef<mlir::Value> Captures) {
  // Note: This function was originally broken out to
  //       unwrap captures which would prevent mutable captures.
  // Do not unwrap captures.
  return create<LambdaOp>(Loc, Name, Captures);
}

std::pair<schir::FuncOp, schir::EnvFrame*>
OpGen::createLambdaFunction(Value Formals, SourceLocation Loc,
                            llvm::StringRef Name) {
  Formals = Context.UnwrapSyntaxClosure(Formals);

  // Ensure we are no longer top level.
  if (!TopLevelOp)
    InsertTopLevelCommandOp(Loc);

  std::string MangledName = mangleFunctionName(Name);
  if (MangledName.empty())
    return {nullptr, nullptr};

  bool HasRestParam = false;
  schir::EnvFrame* EnvFrame = Context.PushLambdaFormals(Formals, HasRestParam);
  if (!EnvFrame)
    return {nullptr, nullptr};
  unsigned Arity = EnvFrame->getBindings().size();
  RestParamKind RPK = HasRestParam ? RestParamKind::List
                                   : RestParamKind::None;
  mlir::FunctionType FT = createFunctionType(Arity, RPK);
  schir::FuncOp FuncOp = createFunction(Loc, FT, MangledName);
  return {FuncOp, EnvFrame};
}

// Compile the body of the lambda and return the result of the LambdaOp.
mlir::Value OpGen::createLambdaBody(SourceLocation Loc,
                                    schir::Value Body,
                                    schir::FuncOp FuncOp,
                                    schir::EnvFrame* EnvFrame) {
  llvm::SmallVector<mlir::Value, 8> Captures;

  // Insert into the function body
  {
    // Start new scope in tail pos
    TailPosScope TPS(*this);
    IsTailPos = true;
    LambdaScope LS(*this, FuncOp);
    mlir::OpBuilder::InsertionGuard IG(Builder);
    mlir::Block& Block = *FuncOp.addEntryBlock();
    Builder.setInsertionPointToStart(&Block);

    // ValueArgs drops the Closure arg at the front
    auto ValueArgs  = Block.getArguments().drop_front();
    // Create the BindingOps for the arguments
    for (auto tup : llvm::zip(EnvFrame->getBindings(),
                              ValueArgs)) {
      auto [B, Arg] = tup;
      createBinding(B, Arg);
    }

    // If Result is null then it already
    // has a terminator.
    if (mlir::Value Result = createBody(Loc, Body)) {
      Result = LocalizeValue(Result);
      ContOp::create(Builder, Result.getLoc(), Result);
    }

    Context.PopEnvFrame(EnvFrame);
    Captures = std::move(LS.Node.Captures);
  }

  return createLambda(Loc, FuncOp.getSymName(), Captures);
}

mlir::Value OpGen::createCaseLambda(Pair* FullExpr) {
  schir::SourceLocation Loc = FullExpr->getSourceLocation();
  Value Cases = FullExpr->Cdr;

  mlir::FunctionType FT = createFunctionType(/*Arity=*/1,
                                             RestParamKind::ValueRefs);

  // Create the containing function.
  schir::FuncOp WFuncOp = createFunction(Loc, FT);
  llvm::SmallVector<mlir::Value, 8> Captures;

  {
    // Start new scope in tail pos
    TailPosScope TPS(*this);
    IsTailPos = true;
    LambdaScope LS(*this, WFuncOp);
    mlir::OpBuilder::InsertionGuard IG(Builder);
    mlir::Block& Block = *WFuncOp.addEntryBlock();
    Builder.setInsertionPointToStart(&Block);
    // The first argument is the Context object.
    mlir::Value InputValueRefs = Block.getArgument(1);

    for (Value Case : Value(Cases)) {
      schir::SourceLocation CaseLoc = Case.getSourceLocation();
      // Create PatternOp for each case
      auto PatternOp = create<schir::PatternOp>(CaseLoc);
      mlir::Block& B = PatternOp.getRegion().emplaceBlock();
      mlir::OpBuilder::InsertionGuard IG(Builder);
      Builder.setInsertionPointToStart(&B);

      mlir::Value Lambda;
      mlir::FunctionType FT;
      if (Pair* LambdaDef = dyn_cast<Pair>(Case)) {
        Value Formals = LambdaDef->Car;
        Value Body = LambdaDef->Cdr;
        auto [FuncOp, EnvFrame] = createLambdaFunction(Formals, CaseLoc);
        if (!FuncOp)
          return Error();
        // We need the function type to match against its args.
        FT = FuncOp.getFunctionType();
        Lambda = createLambdaBody(CaseLoc, Body, FuncOp, EnvFrame);
      }

      if (CheckError())
        return mlir::Value();
      if (!Lambda || !FT)
        return SetError("invalid case-lambda syntax");

      // Match the arguments of the function.
      auto MA = create<MatchArgsOp>(CaseLoc, FT, InputValueRefs);

      // Call the lambda.
      create<ApplyOp>(CaseLoc, Lambda, MA.getResults());
    }

    // Terminate with error call for when no patterns match.
    mlir::Value ErrorMsg = createLiteral(Loc,
        Context.CreateString("no matching case for case-lambda"));
    createError(Loc, ErrorMsg);

    Captures = std::move(LS.Node.Captures);
  }

  return createLambda(Loc, WFuncOp.getSymName(), Captures);
}

void OpGen::PopContinuationScope() {
  mlir::OpBuilder::InsertionGuard IG(Builder);
  LambdaScopeNode& LS = LambdaScopes.back();
  mlir::Location Loc = LS.Op->getLoc();
  assert(isa<FuncOp>(LS.Op) && "expecting a function scope");
  llvm::StringRef MangledName = LS.Op->getAttrOfType<mlir::StringAttr>(
                                  mlir::SymbolTable::getSymbolAttrName())
                                  .getValue();
  assert(LS.CallOp != nullptr && "a PushContOp should precede a call");
  Builder.setInsertionPoint(LS.CallOp);
  PushContOp::create(Builder, Loc, MangledName, LS.Captures);
  LambdaScopes.pop_back();
}

bool OpGen::isLocalDefineAllowed() {
  return IsLocalDefineAllowed;
}

// Does not add the entry block (use addEntryBlock)
schir::FuncOp OpGen::createSyntaxFunction(SourceLocation Loc) {
  mlir::FunctionType FT = createFunctionType(/*Arity=*/2, RestParamKind::None);
  return createFunction(Loc, FT);
}

schir::FuncOp OpGen::createSyntaxFunction(SourceLocation Loc,
                                          schir::Value Proc) {
  llvm_unreachable("not used");
  schir::FuncOp SyntaxFn = createSyntaxFunction(Loc);
  mlir::Block& Body = *SyntaxFn.addEntryBlock();
  mlir::OpBuilder::InsertionGuard IG(Builder);
  Builder.setInsertionPointToStart(&Body);
  TailPosScope TPS(*this);
  IsTailPos = true;
  LambdaScope LS(*this, SyntaxFn);
  mlir::Value Fn = GetSingleResult(Proc);
  auto Args = Body.getArguments().drop_front();
  create<ApplyOp>(Loc, Fn, Args);
  return SyntaxFn;
}

//  - SyntaxSpec is the full <Keyword> <TransformerSpec> pair
//    that is passed to the syntax function.
//  - OrigCall is likely (define-syntax ...) or (let-syntax ...)
mlir::Value OpGen::createSyntaxSpec(Pair* SyntaxSpec, Value OrigCall) {
  Symbol* Keyword = unwrapIdentifier(SyntaxSpec->Car);
  if (!Keyword) return SetError("expecting syntax spec keyword", SyntaxSpec);
  if (!isTopLevel())
    return createLocalSyntaxSpec(SyntaxSpec, OrigCall);

  // Save the insertion point for top level define-syntax.
  mlir::OpBuilder::InsertPoint PrevIp = Builder.saveInsertionPoint();
  Environment* TopLevelEnv = Context.GetTopLevelEnvironment();
  assert(TopLevelEnv && "expecting top level environment");

  std::string MangledName;
  EnvEntry ExistingEntry = TopLevelEnv->Lookup(Context, Keyword);
  if (ExistingEntry.MangledName) {
    // Shadow any previous use of the name.
    MangledName = ExistingEntry.MangledName->getStringRef().str();
    if (mlir::Operation* Old = LookupSymbol(MangledName))
      ShadowGlobalSymbol(Old);
    Context.AddKnownAddress(MangledName, schir::Value(schir::Undefined()));
  } else {
    // Create the syntax as a new global.
    MangledName = mangleVariable(Keyword);
    if (MangledName.empty())
      return Error();
  }
  SourceLocation DefineLoc = OrigCall.getSourceLocation();
  // FIXME This code is very similar to stuff in createTopLevelDefine
  auto GS = createTopLevel<schir::GlobalSyntaxOp>(DefineLoc, MangledName);
  setTopLevelOp(GS.getOperation());
  mlir::Block& Block = *GS.addEntryBlock();
  // Set insertion point.
  Builder.setInsertionPointToStart(&Block);

  schir::SyntaxOp SyntaxOp = createTransformer(SyntaxSpec);
  if (!SyntaxOp)
    return Error();

  // Insert a ContOp for the global that was created.
  TopLevelEnv->Insert(Keyword, Context.CreateIdTableEntry(MangledName));
  ContOp::create(Builder, SyntaxOp.getLoc(), SyntaxOp.getResult());
  Builder.restoreInsertionPoint(PrevIp);
  // Syntax should be available at compile-time.
  Value GlobalOpVal = GS.getOperation();
  Context.RunSync(SCHIR_BASE_VAR(op_eval), GlobalOpVal);
  return mlir::Value();
}

// Bind a keyword to a syntax that captures the frame it is defined in.
mlir::Value OpGen::createLocalSyntaxSpec(Pair* SyntaxSpec, Value OrigCall) {
  if (!IsLocalDefineAllowed)
    return SetError("unexpected define-syntax", OrigCall);

  // Capture the definition frame first.
  auto* Scope = cast<EnvFrame>(Context.getEnvironment());
  llvm::SmallVector<Value, 4> Captures = {Scope};

  // Evaluate the transformer in a temporary command.
  auto Command = createTopLevel<schir::CommandOp>(OrigCall.getSourceLocation());
  auto CommandEraser = llvm::scope_exit([Command] { Command->erase(); });
  Value Transformer;
  {
    mlir::OpBuilder::InsertionGuard IG(Builder);
    Builder.setInsertionPointToStart(Command.addEntryBlock());
    llvm::SaveAndRestore IsLocalDefineAllowedRAII(IsLocalDefineAllowed, false);
    llvm::SaveAndRestore<llvm::SmallVectorImpl<Value>*>
      LocalSyntaxCapturesRAII(LocalSyntaxCaptures, &Captures);
    schir::SyntaxOp SyntaxOp = createTransformer(SyntaxSpec);
    if (!SyntaxOp)
      return Error();
    ContOp::create(Builder, SyntaxOp.getLoc(), SyntaxOp.getResult());
    Value CommandOpVal = Command.getOperation();
    Transformer = Context.RunSync(SCHIR_BASE_VAR(op_eval), CommandOpVal);
  }
  if (auto* Err = dyn_cast_or_null<schir::Error>(Transformer))
    return SetError(Err);

  // Bind a syntax closure of the current environment as its node.
  Value Keyword = SyntaxSpec->Car;
  if (auto* SC = dyn_cast<SyntaxClosure>(Keyword); SC && SC->Env == Scope)
    Keyword = SC->Node;
  auto* TransformerSyntax = cast<Syntax>(Transformer);
  Value LocalSyntax = Context.CreateSyntax(
      TransformerSyntax->getFnData(), Captures,
      TransformerSyntax->isExpandable());
  Context.PushLocalBinding(Context.CreateBinding(Keyword, LocalSyntax));
  return createUndefined();
}

// Create the SyntaxOp of the transformer in a syntax spec.
schir::SyntaxOp OpGen::createTransformer(Pair* SyntaxSpec) {
  Value Transformer = SyntaxSpec->Cdr.car();
  Pair* TransformerSpec =
      Transformer ? dyn_cast<Pair>(Context.UnwrapSyntaxClosure(Transformer))
                  : nullptr;
  Value SyntaxOperator;

  if (TransformerSpec && isIdentifier(TransformerSpec->Car))
    SyntaxOperator = Context.GetSyntax(LookupEnv(TransformerSpec->Car)).Value;

  if (!SyntaxOperator) {
    SetError("invalid syntax spec", SyntaxSpec);
    return {};
  }

  mlir::Value Result = CallSyntax(SyntaxOperator, SyntaxSpec);
  auto SyntaxOp = Result ? Result.getDefiningOp<schir::SyntaxOp>()
                         : schir::SyntaxOp();
  if (CheckError())
    return {};
  if (!SyntaxOp)
    SetError("expecting syntax transformer", SyntaxSpec);
  return SyntaxOp;
}

mlir::Value OpGen::createSyntaxRules(SourceLocation Loc,
                                     Value Keyword,
                                     Value Ellipsis,
                                     schir::Value LiteralList,
                                     schir::Value PatternDefs) {
  // Create (anonymous) syntax function.
  schir::FuncOp FuncOp = createSyntaxFunction(Loc);

  // Expect list of unique literal identifiers.
  llvm::SmallPtrSet<String*, 4> Literals;
  auto insertLiteral = [&](Value V) -> bool {
    // TODO Support bound identifiers for literal list (I think).
    // Store a literal by its name.
    Symbol* S = unwrapIdentifier(V);
    if (!S) {
      SetError("expecting keyword literal", V);
      return true;
    }
    if (S->Equiv(unwrapIdentifier(Ellipsis))) {
      SetError("keyword literal cannot be same as ellipsis", S);
      return true;
    }
    bool Inserted;
    std::tie(std::ignore, Inserted) = Literals.insert(S->getString());
    if (!Inserted) {
      SetError("keyword specified multiple times", S);
      return true;
    }
    return false;
  };
  LiteralList = Context.UnwrapSyntaxClosure(LiteralList);
  while (Pair* X = dyn_cast<Pair>(LiteralList)) {
    Context.setLoc(X);
    if (insertLiteral(X->Car)) {
      return Error();
    }
    LiteralList = Context.UnwrapSyntaxClosure(X->Cdr);
  }
  if (!isa<Empty>(LiteralList)) {
    return SetError("expecting keyword list", LiteralList);
  }
  PatternDefs = Context.UnwrapSyntaxClosure(PatternDefs);
  if (!isa<Pair>(PatternDefs)) {
    return SetError("expecting list of pattern templates pairs", PatternDefs);
  }

  // Create the SyntaxOp
  auto SyntaxOp = create<schir::SyntaxOp>(Loc, FuncOp.getSymName());
  mlir::Block& Body = *FuncOp.addEntryBlock();
  mlir::BlockArgument ExprArg = Body.getArgument(1);
  mlir::BlockArgument EnvArg = Body.getArgument(2);

  mlir::OpBuilder::InsertionGuard IG(Builder);
  Builder.setInsertionPointToStart(&Body);

  // Load the definition frame that a local syntax captures.
  mlir::Value Scope;
  if (LocalSyntaxCaptures)
    Scope = create<LoadRefOp>(Loc, Body.getArgument(0), /*Index=*/0);

  // Iterate through each pattern/template pair.
  assert(!CheckError() && "should not have errors here");
  while (Pair* I = dyn_cast<Pair>(PatternDefs)) {
    schir::Value Pattern;
    schir::Value Template;
    auto* X = dyn_cast<Pair>(Context.UnwrapSyntaxClosure(I->Car));
    Value Rest = X ? Context.UnwrapSyntaxClosure(X->Cdr) : Value();
    if (X && isa_and_nonnull<Empty>(Rest.cdr())) {
      Pattern  = X->Car;
      Template = Rest.car();
    } else {
      return SetError("expecting pattern template pair", I->Car);
    }

    // Create a PatternOp for all but the last pattern.
    mlir::OpBuilder::InsertPoint RestoreIp;
    if (!isa<Empty>(I->Cdr)) {
      auto PatternOp = create<schir::PatternOp>(Pattern.getSourceLocation());
      mlir::Block& B = PatternOp.getRegion().emplaceBlock();
      RestoreIp = Builder.saveInsertionPoint();
      Builder.setInsertionPointToStart(&B);
    }

    PatternTemplate PT(*this, Keyword, Ellipsis, EnvArg, Scope, Literals);
    PT.VisitPatternTemplate(Pattern, Template, ExprArg);
    if (RestoreIp.isSet())
      Builder.restoreInsertionPoint(RestoreIp);
    PatternDefs = Context.UnwrapSyntaxClosure(I->Cdr);
  }

  if (!isa<Empty>(PatternDefs)) {
    return SetError("expecting list of pattern templates pairs", PatternDefs);
  }
  return SyntaxOp.getResult();
}

// Create a sequence of operations in the current block.
mlir::Value OpGen::createSequence(SourceLocation Loc, Value Body) {
  if (CheckError())
    return mlir::Value();
  if (!isa<Pair>(Body)) {
    return SetError(Loc, "sequence must contain an expression", Body);
  }

  Value Rest = Body;
  {
    TailPosScope TPS(*this);
    IsTailPos = false;
    while (true) {
      Pair* Current = cast<Pair>(Rest);
      Rest = Current->Cdr;
      if (isa<Empty>(Rest)) {
        Rest = Current->Car;
        break;
      }
      Visit(Current->Car);
      if (CheckError())
        return Error();
    }
  }
  // This could be in tail position
  return Visit(Rest);
}

// Initialize the binding of a local define from its deferred syntax.
void OpGen::createDefineInit(Binding* B) {
  Value DefineArgs = cast<schir::Undefined>(B->getValue()).getTracer();
  SourceLocation Loc = DefineArgs.getSourceLocation();
  mlir::Value Init = VisitDefineArgs(DefineArgs);
  if (!Init || CheckError())
    return;
  mlir::Value BVal = LocalizeValueOrBinding(BindingTable.lookup(B), B);
  assert(BVal && isa<SchirBindingType>(BVal.getType()) &&
         "BindingTable should have an entry for local define");
  Init = UnwrapBinding(Init);
  create<SetOp>(Loc, BVal, Init);
}

// Check that the definitions of a body are unique and that none changes
// an identifier that phase 1 resolved. Return true on error.
bool OpGen::CheckBodyDefines(EnvFrame* Frame,
                             llvm::ArrayRef<BodyLookup> Lookups) {
  llvm::SmallVector<Binding*, 8> Defines;
  for (Value B : Frame->getAddedBindings())
    Defines.push_back(cast<Binding>(B));
  llvm::SmallPtrSet<Binding*, 8> DefineSet(Defines.begin(), Defines.end());
  IdSet LocalIds;
  for (Binding* B : llvm::reverse(Defines)) {
    Value Id = B->getIdentifier();
    if (!LocalIds.insert(Context.GetIdentifierUniqueId(Id)).second) {
      SetError("local variable has duplicate definitions", Id);
      return true;
    }
  }

  for (const BodyLookup& L : Lookups) {
    EnvEntry Entry = L.IsContextLookup ? Context.Lookup(L.Id, L.Env)
                                       : LookupEnv(L.Id, L.Env);
    // Reject a lookup that now finds a define of this body it did not find.
    auto* B = dyn_cast_or_null<Binding>(Entry.Value);
    if (!B || !DefineSet.contains(B) || Entry.Value == L.Entry.Value)
      continue;
    Symbol* S = unwrapIdentifier(L.Id);
    Value Note = Context.CreateError(B->getIdentifier().getSourceLocation(),
                                     "defined here", Empty());
    SetError(S->getSourceLocation(),
             "identifier is used before its definition in the body: {}",
             {S, Note});
    return true;
  }
  return false;
}

// Expand each form of a body until its head is known, binding its
// definitions, and then generate the deferred definitions and expressions
// in order.
mlir::Value OpGen::createBody(SourceLocation Loc, Value Body) {
  if (CheckError())
    return mlir::Value();
  auto* Frame = cast<EnvFrame>(Context.getEnvironment());

  // Hold a deferred expression or the binding of a define with the
  // syntax stack of the expansion that produced it.
  struct BodyItem {
    Value Form;
    Value Stack;
    Binding* Define = nullptr;
  };
  Value BaseStack = SyntaxStack.getBinding(Context)->getValue();
  llvm::SmallVector<BodyItem, 8> Items;
  llvm::SmallVector<BodyLookup, 8> Lookups;
  {
    llvm::SaveAndRestore IsLocalDefineAllowedRAII(IsLocalDefineAllowed, true);
    llvm::SaveAndRestore<llvm::SmallVectorImpl<BodyLookup>*>
      BodyLookupsRAII(BodyLookups, &Lookups);

    // Take forms and their syntax stacks from the back of the work list.
    llvm::SmallVector<std::pair<Value, Value>, 8> Forms;
    auto pushForms = [&](Value List, Value Stack) {
      size_t Start = Forms.size();
      List = Context.UnwrapSyntaxClosure(List);
      while (auto* P = dyn_cast<Pair>(List)) {
        Forms.push_back({P->Car, Stack});
        List = Context.UnwrapSyntaxClosure(P->Cdr);
      }
      std::reverse(Forms.begin() + Start, Forms.end());
      if (!isa<Empty>(List))
        SetError("expecting proper list in body", List);
      return !CheckError();
    };
    if (!pushForms(Body, BaseStack))
      return Error();

    while (!Forms.empty()) {
      auto [Form, Stack] = Forms.pop_back_val();
      Form = Context.UnwrapSyntaxClosure(Form);
      auto* P = dyn_cast<Pair>(Form);
      Value Operator;
      if (P && isIdentifier(P->Car)) {
        EnvEntry Entry = LookupEnv(P->Car);
        Operator = Context.GetSyntax(Entry).Value;
      }

      // Restore the syntax stack of the expansion for error notes.
      auto SyntaxStackRestorer = setSyntaxStack(Context, Stack);
      auto nextStack = [&, Stack = Stack] {
        return Value(Context.CreatePair(Context.CreatePair(Operator, P),
                                        Stack));
      };

      if (!Operator) {
        Items.push_back({Form, Stack});
      } else if (Operator == Value(SCHIR_BASE_VAR(define))) {
        CallSyntax(Operator, P);
        if (CheckError())
          return Error();
        // Take the binding that the define added.
        Value B = Frame->getAddedBindings().car();
        Items.push_back({Form, Stack, cast<Binding>(B)});
      } else if (Operator == Value(SCHIR_BASE_VAR(define_syntax))) {
        CallSyntax(Operator, P);
      } else if (Operator == Value(SCHIR_BASE_VAR(begin))) {
        pushForms(P->Cdr, nextStack());
      } else if (Operator == Value(SCHIR_BASE_VAR(include))) {
        // Splice the forms of the included file.
        Value Parsed = ExpandSyntax(cast<Syntax>(Operator), P);
        if (CheckError())
          return Error();
        pushForms(Parsed, nextStack());
      } else if (auto* S = dyn_cast<Syntax>(Operator);
                 S && S->isExpandable()) {
        Forms.push_back({ExpandSyntax(S, P), nextStack()});
      } else {
        Items.push_back({Form, Stack});
      }
      if (CheckError())
        return Error();
    }
  }

  if (CheckBodyDefines(Frame, Lookups))
    return Error();
  if (Items.empty())
    return SetError(Loc, "body must end with an expression", Body);
  if (Items.back().Define)
    return SetError("body must end with an expression", Items.back().Form);

  // Generate an item with the syntax stack of its expansion.
  auto visitItem = [&](BodyItem& Item) -> mlir::Value {
    auto SyntaxStackRestorer = setSyntaxStack(Context, Item.Stack);
    if (Item.Define) {
      createDefineInit(Item.Define);
      return mlir::Value();
    }
    return Visit(Item.Form);
  };

  {
    TailPosScope TPS(*this);
    IsTailPos = false;
    for (BodyItem& Item : llvm::drop_end(Items)) {
      visitItem(Item);
      if (CheckError())
        return Error();
    }
  }
  // Visit the last item in the current tail position.
  return visitItem(Items.back());
}

mlir::Value OpGen::createBinding(Binding *B, mlir::Value Init) {
  SourceLocation SymbolLoc = B->getSourceLocation();
  mlir::Value BVal = createHelper<BindingOp>(Builder, SymbolLoc, Init);
  BindingTable.insert(B, BVal);

  return BVal;
}

mlir::Value OpGen::createDefine(Value Id, Value DefineArgs, Value OrigCall) {
  if (isTopLevel()) return createTopLevelDefine(Id, DefineArgs, OrigCall);
  if (!IsLocalDefineAllowed) return SetError("unexpected define", OrigCall);
  // Bind a syntax closure of the current environment as its symbol.
  if (auto* SC = dyn_cast<SyntaxClosure>(Id);
      SC && SC->Env == Context.getEnvironment())
    Id = SC->Node;
  // Create the binding with an undefined object that secretly holds
  // its syntax in the current environment.
  Binding* B = Context.CreateBinding(Id, Undefined(DefineArgs));
  // Push to the local environment.
  Context.PushLocalBinding(B);
  return createBinding(B, mlir::Value());
}

std::tuple<EnvEntry, Symbol*, String*>
OpGen::createTopLevelBindingInfo(Value Id) {
  std::tuple<EnvEntry, Symbol*, String*> Result;
  auto& [Entry, S, MangledName] = Result;

  if (LibraryEnvProc) {
    SetError("unexpected define");
    return Result;
  }

  assert(isTopLevel() && "expecting top level");
  assert(isIdentifier(Id) && "expecting identifier");

  Environment* Env = nullptr;
  // Unwrap SyntaxClosures.
  if (auto* SC = dyn_cast<SyntaxClosure>(Id)) {
    S = cast<Symbol>(SC->Node);
    if (auto* EF = dyn_cast<EnvFrame>(SC->Env)) {
      // Bind a template-introduced name to an anonymous global.
      Entry = EF->LookupAddedBindings(S);
      if (Entry.MangledName) {
        MangledName = Entry.MangledName;
        return Result;
      }
      MangledName = Context.CreateIdTableEntry(mangleAnonymousId());
      auto* EN = Context.CreateExternName(S->getSourceLocation(),
                                          MangledName);
      Context.AddBinding(EF, Context.CreateBinding(S, EN));
      return Result;
    }
    Env = cast<Environment>(SC->Env);
  } else {
    S = cast<Symbol>(Id);
    Env = Context.GetTopLevelEnvironment();
  }

  assert(Env && "expecting top level environment");

  Entry = Env->Lookup(Context, S);
  if (Entry.Value && Entry.MangledName) {
    if (Mangler::isExternalVariable(schir::ManglePrefix,
                                    getModulePrefix(),
                                    Entry.MangledName->getView())) {
      SetError("define overwrites imported global", S);
      return Result;
    }
  }

  if (Entry.MangledName) {
    MangledName = Entry.MangledName;
    return Result;
  }

  schir::Mangler Mangler = createMangler();
  std::string MangledNameStr = Mangler.mangleVariable(getModulePrefix(), S);
  if (MangledNameStr.empty())
    return Result;
  MangledName = Context.CreateIdTableEntry(MangledNameStr);

  // Update any existing export.
  UpdateExports(S->getString(), MangledName);

  // Insert into the module level scope
  Env->Insert(S, MangledName);

  return Result;
}

mlir::Value OpGen::createTopLevelDefine(Value Id, Value DefineArgs,
                                        Value OrigCall) {
  SourceLocation DefineLoc = OrigCall.getSourceLocation();
  auto&& [Entry, S, MangledName] = createTopLevelBindingInfo(Id);
  if (CheckError())
    return Error();

  if (Entry.MangledName) {
    llvm::StringRef MangledNameRef = Entry.MangledName->getStringRef();
    mlir::Operation* Old = LookupSymbol(MangledNameRef);
    if (Old && isa<schir::GlobalSyntaxOp>(Old)) {
      // Shadow a syntax name if it exists.
      ShadowGlobalSymbol(Old);
      Context.AddKnownAddress(MangledNameRef, schir::Value(schir::Undefined()));
    } else {
      // If a variable of the name already exists in the
      // current module then it behaves like `set!`.
      TailPosScope TPS(*this);
      IsTailPos = false;
      mlir::Value Init = VisitDefineArgs(DefineArgs);
      if (!Init)
        return mlir::Value();
      mlir::Value LocalV = create<LoadGlobalOp>(DefineLoc, MangledNameRef);
      return create<SetOp>(DefineLoc, LocalV, Init);
    }
  }

  assert(MangledName && !MangledName->getStringRef().empty());

  // It is possible someone used a variable name not in scope
  // so we created a global and they are defining it now.
  llvm::StringRef MangledNameRef = MangledName->getStringRef();
  auto GlobalOp = dyn_cast_or_null<schir::GlobalOp>(
      LookupSymbol(MangledNameRef));
  if (GlobalOp) {
    // Move the GlobalOp to the current ModuleBuilder insert point
    GlobalOp->moveBefore(ModuleBuilder.getInsertionBlock(),
                         ModuleBuilder.getInsertionPoint());
  } else {
    GlobalOp = createTopLevel<schir::GlobalOp>(DefineLoc, MangledNameRef,
        Builder.getType<SchirBindingType>());
  }
  setTopLevelOp(GlobalOp.getOperation());
  mlir::Block& Block = *GlobalOp.addEntryBlock();

  {
    // set insertion point. add initializer
    mlir::OpBuilder::InsertionGuard IG(Builder);
    Builder.setInsertionPointToStart(&Block);

    if (mlir::Value Init = VisitDefineArgs(DefineArgs)) {
      mlir::Value BVal = create<BindingOp>(DefineLoc, Init);
      // This is the only place ContOp should accept a !schir.binding.
      create<ContOp>(DefineLoc, BVal);
    }
  }

  return mlir::Value();
}

mlir::Value OpGen::createExternalBinding(Value Id, Value ExtSymbol) {
  assert(!ExtSymbol.getStringRef().empty());
  if (!isTopLevel())
    return SetError("unexpected define-binding");
  schir::SourceLocation Loc = Id.getSourceLocation();
  auto&& [_, S, MangledName] = createTopLevelBindingInfo(Id);
  if (CheckError())
    return Error();

  if (!MangledName || LookupSymbol(MangledName->getStringRef()))
    return SetError("unable to bind to existing define");

  auto Op = createTopLevel<schir::GlobalBindingOp>(Loc,
                                         MangledName->getStringRef(),
                                         ExtSymbol.getStringRef());
  setTopLevelOp(Op.getOperation());
  return mlir::Value();
}

mlir::Value OpGen::createIf(SourceLocation Loc, Value Cond, Value Then,
                            Value Else) {
  // Cond
  mlir::Value CondResult;
  {
    TailPosScope TPS(*this);
    IsTailPos = false;
    // Local defines are finished in here.
    CondResult = GetSingleResult(Cond);
  }

  bool TailPos = isTailPos();

  mlir::Block* ThenBlock = new mlir::Block();
  mlir::Block* ElseBlock = new mlir::Block();

  auto IfContOp = create<schir::IfContOp>(Loc, CondResult);
  IfContOp.getThenRegion().push_back(ThenBlock);
  IfContOp.getElseRegion().push_back(ElseBlock);

  {
    // Treat then/else regions as tail position since if there is
    // any ApplyOp contained therein the whole operation becomes
    // an IfContOp which is a terminator with its own PushContOp.
    TailPosScope TPS(*this);
    IsTailPos = true;

    auto handleBranch = [&](schir::Value Value, mlir::Block* Block) {
      LambdaScope LS(*this, IfContOp);
      mlir::OpBuilder::InsertionGuard IG(Builder);
      Builder.setInsertionPointToStart(Block);
      mlir::Value Result = Visit(Value);
      Block = Builder.getBlock();
      if (Result) {
        assert(Block->empty() ||
          !Block->back().hasTrait<mlir::OpTrait::IsTerminator>());
        Result = UnwrapBinding(Result);
        create<ContOp>(Loc, Result);
      }
    };

    handleBranch(Then, ThenBlock);
    handleBranch(Else, ElseBlock);
  }

  if (TailPos) return mlir::Value();
  return createContinuation(IfContOp);
}

// LHS can be a symbol or a binding
mlir::Value OpGen::createSet(SourceLocation Loc, Value LHS,
                                                 Value RHS) {
  TailPosScope TPS(*this);
  IsTailPos = false;
  assert((isa<Binding, Symbol, ExternName, SyntaxClosure>(LHS)) &&
      "expects a Symbol or Binding for LHS");
  // ExprVal must be evaluated first so
  // the Binding will be in the continuation scope
  mlir::Value ExprVal = GetSingleResult(RHS);
  mlir::Value BVal = GetSingleResultOrBinding(LHS);
  assert((isa<SchirBindingType, SchirUnknownType>(BVal.getType())));
  return create<SetOp>(Loc, BVal, ExprVal);
}

// This handles everything after the `define` keyword
// including terse lambda syntax. This supports lazy
// visitation of local bindings' initializers.
mlir::Value OpGen::VisitDefineArgs(Value Args) {
  Pair* P = cast<Pair>(Args);
  mlir::Value Result;
  if (Pair* LambdaSpec = dyn_cast<Pair>(P->Car)) {
    // We already checked the name.
    Value Id = LambdaSpec->Car;
    assert(isIdentifier(Id) && "expecting identifier");
    Value Formals = LambdaSpec->Cdr;
    Value Body = P->Cdr;
    llvm::StringRef Name = isTopLevel() ? Id.getStringRef()
                                        : llvm::StringRef();
    Result = createLambda(Formals, Body,
                          Id.getSourceLocation(),
                          Name);
  } else if (isIdentifier(P->Car) && isa<Pair>(P->Cdr)) {
    Result = Visit(cast<Pair>(P->Cdr)->Car);
  } else {
    Result = SetError("invalid define syntax", P);
  }

  // TODO Use MatchArgsOp to destructure ValueRefs to a single
  //      argument (or raise an error.)
  if (Result && isa<SchirValueRefsType>(Result.getType()))
    Result = create<LoadRefOp>(schir::SourceLocation(), Result, /*Index*/0);

  return Result;
}

// createGlobal - Create or load a global idempotently.
//                Prevent duplicate Ops in an isolated scope.
mlir::Value OpGen::createGlobal(SourceLocation Loc,
                                llvm::StringRef MangledName) {
  mlir::ModuleOp M = getModuleOp();
  Operation* G = LookupSymbol(MangledName);

  if (!G) {
    // Lazily insert external GlobalOp
    // at the beginning of the module.
    // Note that OpEval will never visit these.

    // Try to get the type of the global defaulting to !schir.unknown.
    schir::Value KnownVal = Context.GetKnownValue(MangledName);
    mlir::Type Type = KnownVal
      ? getValueType(Context.MLIRContext.get(), KnownVal)
      : SchirUnknownType::get(Context.MLIRContext.get());
    mlir::OpBuilder::InsertionGuard IG(ModuleBuilder);
    ModuleBuilder.setInsertionPointToStart(M.getBody());
    G = createTopLevel<GlobalOp>(Loc, MangledName, Type)
      .getOperation();
  }

  // Wrap the mlir::Operation and use for the lookup.
  auto CanonicalValue = schir::Value(G);
  auto SymbolOp = cast<mlir::SymbolOpInterface>(G);

  // Localize globals by the GlobalOp's Operation*
  mlir::Value LocalV;
  // If no TopLevelOp exists then no binding scope exists either.
  if (TopLevelOp)
    LocalV = BindingTable.lookup(CanonicalValue);
  if (!LocalV) {
    mlir::Type Type = isa<schir::GlobalOp>(G)  // Could be GlobalBindingOp
      ? cast<GlobalOp>(G).getType()
      : SchirUnknownType::get(Context.MLIRContext.get());
    LocalV = create<LoadGlobalOp>(Loc, Type, SymbolOp.getName());
    BindingTable.insert(CanonicalValue, LocalV);
    return LocalV;
  }

  return LocalizeValueOrBinding(LocalV, CanonicalValue);
}

mlir::Value OpGen::VisitExternName(ExternName* EN) {
  return createGlobal(EN->getSourceLocation(), EN->getView());
}

mlir::Value OpGen::VisitSyntaxClosure(SyntaxClosure* SC) {
  // A SyntaxClosure wraps either a Symbol or a rebuilt expression that
  // substitutes all symbols as SyntaxClosures so we can simply unwrap
  // the latter.
  if (isIdentifier(SC->Node))
    return VisitIdentifier(SC);
  else
    return Visit(SC->Node);
}

mlir::Value OpGen::VisitSymbol(Symbol* S) {
  return VisitIdentifier(S);
}

mlir::Value OpGen::VisitIdentifier(Value Id) {
  EnvEntry Entry = LookupEnv(Id);
  auto* SC = dyn_cast<SyntaxClosure>(Id);
  Symbol* S = unwrapIdentifier(Id);
  SourceLocation Loc = S->getSourceLocation();

  if (!Entry) {
    if (!Context.GetRootEnvironment(SC ? SC->Env : Value()))
      return SetError("identifier is undefined in syntax closure: {}", S);
    // Default to the name of a global
    schir::Mangler Mangler = createMangler();
    std::string MangledName = Mangler.mangleVariable(getModulePrefix(), S);
    if (MangledName.empty())
      return Error();
    return createGlobal(Loc, MangledName);
  }

  return VisitEnvEntry(Loc, Entry);
}

mlir::Value OpGen::VisitEnvEntry(schir::SourceLocation Loc, EnvEntry Entry) {
  llvm::StringRef MangledName;
  if (Entry.MangledName)
    MangledName = Entry.MangledName->getStringRef();

  if (!MangledName.empty()) {
    return createGlobal(Loc, MangledName);
  }

  if (auto* B = dyn_cast<Binding>(Entry.Value); B && isa<Syntax>(B->getValue()))
    return SetError(Loc, "syntax keyword used as a value: {}",
                    B->getIdentifier());

  return GetSingleResultOrBinding(Entry.Value);
}

mlir::Value OpGen::VisitBinding(Binding* B) {
  if (auto* E = dyn_cast<ExternName>(B->getValue()))
    return create<LoadGlobalOp>(Context.getLoc(), E->getView());

  mlir::Value V = BindingTable.lookup(B);
  assert(V && "binding must map to a mlir.value");
  return LocalizeValueOrBinding(V, B);
}

mlir::Value OpGen::HandleCall(Pair* P, schir::EnvEntry FnEnvEntry) {
  schir::SourceLocation Loc = P->getSourceLocation();
  mlir::Value Fn;
  llvm::SmallVector<mlir::Value, 16> Args;
  {
    TailPosScope TPS(*this);
    IsTailPos = false;

    if (FnEnvEntry)
      Fn = VisitEnvEntry(Loc, FnEnvEntry);
    else
      Fn = GetSingleResult(P->Car);

    // Insert the check that Fn is a procedure.
    Fn = CheckType(Loc, Fn, Builder.getType<SchirProcedureType>());

    if (CheckError())
      return Error();

    Value V = P->Cdr;
    while (auto* P2 = dyn_cast<Pair>(V)) {
      mlir::Value Arg = GetSingleResult(P2->Car);
      if (CheckError())
        return Error();
      if (mlir::Operation* Op = Arg.getDefiningOp()) {
        Op->setLoc(mlir::OpaqueLoc::get(
              P2->getSourceLocation().getOpaqueEncoding(),
              Builder.getContext()));
      }
      Args.push_back(Arg);
      V = P2->Cdr;
    }
    if (!isa<Empty>(V)) {
      return SetError("improper list as call expression", P);
    }
  }
  return createCall(Loc, Fn , Args);
}

mlir::Value OpGen::createCall(schir::SourceLocation Loc, mlir::Value Fn,
                              llvm::MutableArrayRef<mlir::Value> Args) {
  // Localize all of the operands
  Fn = LocalizeValue(Fn);
  for (mlir::Value& Arg : Args)
    Arg = LocalizeValue(Arg);

  assert(Loc.isValid() && "expecting a valid location for call");
  auto Op = create<ApplyOp>(Loc, Fn, Args);

  if (IsTailPos) return mlir::Value();
  return createContinuation(Op);
}

mlir::Value OpGen::createError(schir::SourceLocation Loc,
                               llvm::MutableArrayRef<mlir::Value> Args) {
  // We defer localizing values until instantiation.
  mlir::Type SchirValueTy = Builder.getType<SchirValueType>();
  mlir::Value ErrorFn = create<LoadGlobalOp>(Loc, SchirValueTy,
      SCHIR_BASE_VAR_STR(error));
  create<ApplyOp>(Loc, ErrorFn, Args);
  return mlir::Value();
}

mlir::Value OpGen::createOpGen(SourceLocation Loc, mlir::Value Input) {
  // OpGenOp should always be considered tail position.
  create<schir::OpGenOp>(Loc, Input);
  return mlir::Value();
}

// The CallOp is the terminator op that the PushContOp should precede
// in the block.
mlir::Value OpGen::createContinuation(mlir::Operation* CallOp) {
  mlir::FunctionType FT = createFunctionType(/*Arity=*/1,
                                             RestParamKind::ValueRefs);
  // No source location
  schir::SourceLocation Loc;

  // Create the continuation's function
  // subsequent operations will be nested within
  // relying on previous insertion guards to pull us out.
  auto F = createFunction(Loc, FT);
  PushContinuationScope(F, CallOp);
  mlir::Block* FuncEntry = F.addEntryBlock();
  Builder.setInsertionPointToStart(FuncEntry);

  // Return ValueRefs object.
  return FuncEntry->getArguments()[1];
}

mlir::Value OpGen::CallSyntax(Value Operator, Pair* P) {
  auto SyntaxStackPopper = pushSyntaxStack(Context, Operator, P);

  switch (Operator.getKind()) {
    case ValueKind::BuiltinSyntax: {
      Context.setLoc(P->getSourceLocation());
      BuiltinSyntax* BS = cast<BuiltinSyntax>(Operator);
      return BS->Fn(*this, P);
    }
    case ValueKind::Syntax: {
      return toValue(RunSyntax(cast<Syntax>(Operator), P));
    }
    default: {
      llvm_unreachable("expecting syntax");
      return mlir::Value();
    }
  }
}

// Call a syntax with the current environment as its usage environment.
Value OpGen::RunSyntax(Syntax* S, Pair* P) {
  Context.setLoc(P->getSourceLocation());
  std::array<schir::Value, 2> Args{P, Context.getEnvironment()};
  return RunNested(S, Args);
}

// Run a function in a nested loop and propagate its error.
Value OpGen::RunNested(Value Fn, ValueRefs Args) {
  schir::OpGen* OpGen = Context.OpGen;
  assert(OpGen == this && "sanity check");
  ++RunSyncDepth;
  Value Result = Context.RunSync(Fn, Args);
  --RunSyncDepth;
  assert(OpGen == Context.OpGen && "OpGen should not unwind itself");

  if (auto ResultErr = dyn_cast_or_null<schir::Error>(Result)) {
    // Propagate the error to this instance.
    SetError(ResultErr);
  }
  return Result;
}

// Return the form that a syntax function expands to.
Value OpGen::ExpandSyntax(Syntax* S, Pair* P) {
  auto SyntaxStackPopper = pushSyntaxStack(Context, S, P);
  llvm::SaveAndRestore IsExpandOnlyRAII(IsExpandOnly, true);
  return RunSyntax(S, P);
}

mlir::Value OpGen::SetError(schir::Error* NewErr) {
  assert((!Err || Value(NewErr) == Err) && "no squashing errors");
  bool IsNew = !Err;
  Err = NewErr;
  Context.setLoc(Err.getSourceLocation());
  Value Stack = SyntaxStack.getBinding(Context)->getValue();
  bool IsInExpansion = llvm::any_of(Stack, [](Value Node) {
      auto* P = dyn_cast<Pair>(Node);
      return P && isa<Syntax>(P->Car);
    });
  if (IsNew && IsInExpansion) {
    // Recreate the error appending the nodes of the syntax stack as notes.
    llvm::SmallVector<Value, 12> Irrs;
    for (Value SyntaxStackNode : Stack) {
      auto* P = dyn_cast<Pair>(SyntaxStackNode);
      if (!P)
        continue;
      Symbol* S = unwrapIdentifier(P->Cdr.car());
      assert(S && "expecting syntax identifier");
      Value Note = Context.CreateError(P->Cdr.getSourceLocation(),
                                       "while expanding syntax: {}",
                                       Value(S));
      Irrs.insert(Irrs.begin(), Note);
    }
    Value PrevIrrs = NewErr->getIrritants();
    Irrs.insert(Irrs.begin(), PrevIrrs.begin(), PrevIrrs.end());
    Err = Context.CreateError(Value(NewErr).getSourceLocation(),
                              NewErr->getMessage(),
                              Context.CreateList(Irrs));
  }
  if (RunSyncDepth == 0)
    Context.Raise(Err);
  else
    Context.Yield(Err);
  return Error();
}

mlir::Value OpGen::VisitPair(Pair* P) {
  schir::EnvEntry FnEnvEntry;
  schir::Value SyntaxOperator;

  if (isIdentifier(P->Car)) {
    FnEnvEntry = LookupEnv(P->Car);
    SyntaxOperator = Context.GetSyntax(FnEnvEntry).Value;
  }

  if (SyntaxOperator)
    return CallSyntax(SyntaxOperator, P);
  else
    return HandleCall(P, FnEnvEntry);
}

mlir::Value OpGen::VisitVector(Vector* V) {
  llvm::SmallVector<mlir::Value, 8> Vals;
  for (auto X : V->getElements()) {
    mlir::Value Val = GetSingleResult(X);
    if (CheckError())
      return mlir::Value();
    Vals.push_back(Val);
  }

  // Localize the values in the current context.
  for (mlir::Value& Val : Vals)
    Val = LocalizeValue(Val);

  return create<schir::VectorOp>(Context.getLoc(), Vals);
}

// LocalizeValue - If a value belongs to a parent region from
//                 which the current region should be isolated
//                 we generate one of two operations (LoadGlobal or
//                 LoadCapture), and we return the result of that.
//                 Tracking of captures for nested scopes is handled
//                 here too.
//
mlir::Value OpGen::LocalizeValue(mlir::Value V, schir::Value B) {
  mlir::Value Result = LocalizeValueOrBinding(V, B);
  return UnwrapBinding(Result);
}

mlir::Value OpGen::UnwrapBinding(mlir::Value MV) {
  if (isa<SchirBindingType, SchirUnknownType>(MV.getType()))
    MV = create<UnboxOp>(schir::SourceLocation(),
                         Builder.getType<SchirValueType>(),
                         MV);
  return MV;
}

mlir::Value OpGen::LocalizeValueOrBinding(mlir::Value V, schir::Value B) {
  mlir::Operation* Owner;
  LambdaScopeIterator LSI = LambdaScopes.rbegin();

  if (mlir::Operation* Op = V.getDefiningOp()) {
    Owner = Op->getParentWithTrait<mlir::OpTrait::IsIsolatedFromAbove>();
  } else {
    mlir::BlockArgument BlockArg = mlir::cast<mlir::BlockArgument>(V);
    Owner = BlockArg.getOwner()->getParentOp();
  }

  // Non-globals must be captured in every closure up to the scope
  // where they were created.
  return LocalizeRec(B, V, Owner, LSI);
}

mlir::Value OpGen::LocalizeRec(schir::Value B,
                               mlir::Value V,
                               mlir::Operation* Owner,
                               LambdaScopeIterator LSI) {
  assert(LSI != LambdaScopes.rend() && "value must be in a scope");
  LambdaScopeNode& LS = *LSI;
  if (LS.Op == Owner) return V;

  schir::SourceLocation Loc = {};

  mlir::Value ParentLocal = LocalizeRec(B, V, Owner, ++LSI);

  if (!LS.Op->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>())
    return ParentLocal;

  // Capture V for use in the current function scope.
  mlir::OpBuilder::InsertionGuard IG(Builder);
  auto FuncOp = cast<schir::FuncOp>(LS.Op);
  mlir::Block& Block = FuncOp.getBody().front();
  Builder.setInsertionPointToStart(&Block);
  LS.Captures.push_back(ParentLocal);
  uint32_t Index = LS.Captures.size() - 1;
  mlir::Value Closure = Block.getArguments().front();
  mlir::Value NewVal = create<LoadRefOp>(Loc, ParentLocal.getType(),
                                         Closure, Index);

  if (B) {
    BindingTable.insertIntoScope(&LS.BindingScope_, B, NewVal);
  }
  return NewVal;
}

mlir::Operation* OpGen::LookupSymbol(llvm::StringRef MangledName) {
  mlir::ModuleOp M = getModuleOp();
  Operation* G = M.lookupSymbol(MangledName);
  if (G) return G;

  // FIXME This needs to account for symbols in sibling modules
  //       I think we should only see imported names
  M = cast<mlir::ModuleOp>(Context.getModuleOp());
  return M.lookupSymbol(MangledName);
}

// Anonymize an existing symbol so we can shadow it.
void OpGen::ShadowGlobalSymbol(mlir::Operation* Old) {
  mlir::ModuleOp M = getModuleOp();
  mlir::StringAttr OldName = mlir::SymbolTable::getSymbolName(Old);
  std::string NewNameStr = mangleAnonymousId();
  mlir::StringAttr NewName = Builder.getStringAttr(NewNameStr);
  (void)mlir::SymbolTable::replaceAllSymbolUses(OldName, NewName, M);
  mlir::SymbolTable::setSymbolName(Old, NewNameStr);
}

void OpGen::Export(Value NameList) {
  // Iterate the names and add ExportIdOps.
  for (auto [Loc, Value] : WithSource(NameList)) {
    Symbol* Source = dyn_cast<Symbol>(Value);
    Symbol* Target = Source;
    if (!Target) {
      if (Pair* P2 = dyn_cast<Pair>(Value)) {
        if (isSymbol(P2->Car, "rename") && isa<Pair>(P2->Cdr)) {
          Pair* P3 = cast<Pair>(P2->Cdr);
          Source = dyn_cast<Symbol>(P3->Car);
          if (!Source) {
            SetError("expecting identifier", P3);
            return;
          }
          if (Pair* P4 = dyn_cast<Pair>(P3->Cdr)) {
            Target = dyn_cast<Symbol>(P4->Car);
            if (!Target) {
              SetError("expecting identifier", P4);
              return;
            }
            if (!isa<Empty>(P4->Cdr)) {
              SetError("expecting proper list", P4);
              return;
            }
          }
        }
      }
    }
    if (!Source || !Target) {
      SetError(Loc, "expecting export spec", Value);
      return;
    }

    // Op may be default constructed.
    ExportIdOp& Op = Exports[Target->getString()];

    // If the variable or syntax does not exist yet
    // leave out the symbolName to allow binding later.
    // (via createGlobal)
    EnvEntry Entry = LookupEnv(Source);
    llvm::StringRef MangledName = Entry.MangledName
                                ? Entry.MangledName->getStringRef()
                                : llvm::StringRef();

    if (Op && !Op.getSymbolName().empty() &&
        Op.getSymbolName() != MangledName) {
      SetError(Loc, "previous export incompatible definition: {}",
               {Target});
      return;
    } else if (Op) {
      Op.setSymbolName(MangledName);
    } else if (!Op) {
      Op = createHelper<ExportIdOp>(ModuleBuilder, Target->getSourceLocation(),
                                    MangledName, Target->getStringRef(),
                                    Source->getStringRef());
    }

    // Register renames to create ExportIdOps upon
    // importing or defining the name.
    if (MangledName.empty() && Source != Target)
      RenameExports.push_back({Source->getString(), Target->getString()});
  }

  return Context.Cont();
}

void OpGen::Import(schir::ImportSet* ImportSet) {
  Environment* Env = Context.GetTopLevelEnvironment();
  if (!Env) {
    // Import doesn't work in local scope.
    SetError("unexpected import", ImportSet);
    return;
  }

  // FIXME This whole ImportSet::Iterator junk prevents
  //       multiple renames of the same name.

  llvm::sort(RenameExports);
  for (auto&& [Name, MangledName] : *ImportSet) {
    // If there is no name just skip it (it was filtered out).
    if (!Name) continue;
    ImportValue(Env, Name, MangledName);
    if (CheckError())
      return;
  }
}

void OpGen::ImportValue(Environment* Env, String* Name, String* MangledName) {
  if (!Env->ImportValue(Name, MangledName)) {
    SetError("imported name already exists: {}", Name);
    return;
  }
  UpdateExports(Name, MangledName);
}

void OpGen::UpdateExports(String* Name, String* MangledName) {
  auto maybeUpdateExport = [&](schir::String* Name, ExportIdOp Export) {
    llvm::StringRef ExportMangledName = Export.getSymbolName();
    if (ExportMangledName.empty())
      Export.setSymbolName(MangledName->getStringRef());
    else if (ExportMangledName != MangledName->getStringRef())
      SetError("name overwrites previous define or import: {}", Name);
  };
  auto isRename = [&](auto Pair) {
      auto [EnvName, ExportName] = Pair;
      return EnvName == Name;
    };

  // Update export if any.
  if (ExportIdOp Export = Exports.lookup(Name))
    maybeUpdateExport(Name, Export);

  if (!RenameExports.empty()) {
    auto Range = llvm::make_filter_range(RenameExports, isRename);
    for (auto [EnvName, ExportName] : Range) {
      if (ExportIdOp Export = Exports.lookup(ExportName)) {
        maybeUpdateExport(ExportName, Export);
        if (CheckError())
          return;
      }
    }
    llvm::remove_if(RenameExports, isRename);
  }
}

schir::EnvEntry OpGen::LookupEnv(Value Id, Value Env) {
  assert(isIdentifier(Id) && "expecting an identifier");
  if (!Env)
    Env = Context.getEnvironment();

  EnvEntry Result;
  // Find a binding of the syntax closure itself in Env, and then
  // look up its node in its closed environment.
  auto* SC = dyn_cast<SyntaxClosure>(Id);
  if (SC)
    Result = Context.Lookup(SC, Env);
  if (SC && !Result) {
    llvm::SaveAndRestore<llvm::SmallVectorImpl<BodyLookup>*>
      BodyLookupsRAII(BodyLookups, nullptr);
    Result = LookupEnv(SC->Node, SC->Env);
  } else if (!SC) {
    Result = Context.Lookup(Id, Env);
  }

  if (BodyLookups)
    BodyLookups->push_back({Id, Env, Result});
  return Result;
}

void OpGen::RecordContextLookup(Value Id, EnvEntry Entry) {
  if (BodyLookups)
    BodyLookups->push_back({Id, Context.getEnvironment(), Entry,
                            /*IsContextLookup=*/true});
}
