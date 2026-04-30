//===--- Nbdl.cpp - Nbdl binding syntax for SchirScheme ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines syntax (nbdl impl) bindings for SchirScheme.
//
//===----------------------------------------------------------------------===//

#include <nbdl_gen/Dialect.h>
#include <schir/Context.h>
#include <schir/MlirHelper.h>
#include <schir/Nbdl.h>
#include <schir/Value.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <optional>
#include <tuple>

namespace nbdl_gen {
std::tuple<std::string, schir::SourceLocationEncoding*, mlir::Operation*>
translate_cpp(schir::LexerWriterFnRef FnRef, mlir::Operation* Op);
}

namespace schir::nbdl_bind_var {
schir::ContextLocal current_nbdl_module;
schir::ExternFunction translate_cpp;
schir::ExternFunction close_previous_scope;
schir::ExternFunction build_match_params_impl;
schir::ExternFunction build_overload_impl;
schir::ExternFunction build_match_if_impl;
schir::ExternFunction build_context_impl;
schir::ExternFunction build_match_op_impl;
}

namespace {
// Create a builder that appends to the nbdl module.
std::optional<mlir::OpBuilder> getModuleBuilder(schir::Context& C) {
  schir::Value V = schir::nbdl_bind_var::current_nbdl_module.get(C);
  mlir::Operation* Op = schir::cast<mlir::Operation>(V);
  mlir::ModuleOp ModuleOp = schir::dyn_cast_or_null<mlir::ModuleOp>(Op);
  if (!ModuleOp) {
    schir::Error* E = C.CreateError(C.getLoc(),
        "invalid current-nbdl-module", schir::Empty());
    C.Raise(E);
    return {};
  }
  mlir::OpBuilder Builder(ModuleOp);
  Builder.setInsertionPointToEnd(ModuleOp.getBody());
  return Builder;
}
}

namespace schir::nbdl_bind {
// Create a function and call the thunk with a new builder
// to insert into the function body.
// _num_store_params_ N
// _callback_ takes _num_store_params_ + 1 arguments which are the block arguments
//  with formals like (store1 store2 ... storeN fn)
// (%build_match_params _name_ _num_store_params_ _callback_)
void build_match_params_impl(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");

  std::optional<mlir::OpBuilder> BuilderOpt = getModuleBuilder(C);
  if (!BuilderOpt)
    return;
  mlir::OpBuilder Builder = BuilderOpt.value();

  schir::SourceLocation Loc = Args[0].getSourceLocation();
  // Require a schir::Symbol so it has a source location.
  schir::Symbol* NameSym = dyn_cast<schir::Symbol>(Args[0]);
  llvm::StringRef Name = NameSym->getStringRef();
  schir::Value NumParamsVal = Args[1];
  schir::Value Callback = Args[2];

  int32_t NumParams = isa<schir::Int>(NumParamsVal)
      ? int32_t(cast<schir::Int>(NumParamsVal))
      : int32_t(-1);

  if (NumParams < 0)
    return C.RaiseError("expecting positive integer for num_store_params");
  if (!NameSym || Name.empty())
    return C.RaiseError("expecting function name (symbol literal)");

  mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                             Builder.getContext());

  // Create the function type.
  llvm::SmallVector<mlir::Type, 8> InputTypes;
  for (unsigned i = 0; i < static_cast<uint32_t>(NumParams); i++)
    InputTypes.push_back(Builder.getType<nbdl_gen::StoreType>());

  // Push the visitor fn argument.
  InputTypes.push_back(Builder.getType<nbdl_gen::StoreType>());

  mlir::FunctionType FT = Builder.getFunctionType(InputTypes,
                                                  /*ResultTypes*/{});

  // Create the function.
  auto FuncOp = mlir::func::FuncOp::create(Builder, MLoc, Name, FT);
  FuncOp.addEntryBlock();

  schir::Value Thunk = C.CreateLambda([FuncOp](Context& C, ValueRefs) mutable {
    schir::Value Callback = C.getCapture(0);
    llvm::SmallVector<schir::Value, 8> BlockArgs;
    assert(!FuncOp.getBody().empty() && "should have entry block");
    for (mlir::Value MVal : FuncOp.getBody().getArguments()) {
      schir::Value V = C.CreateAny(MVal);
      BlockArgs.push_back(V);
    }

    C.PushCont([FuncOp](Context& C, ValueRefs) mutable {
      C.Cont(FuncOp.getOperation());
    }, CaptureList{});
    C.Apply(Callback, BlockArgs);
  }, CaptureList{Callback});

  // Call the thunk with a Builder at the entry point.
  Builder = mlir::OpBuilder(FuncOp.getBody());
  mlir_helper::with_builder_impl(C, Builder, Thunk);
}

// _callback_ takes a single block argument if specified.
// (%build_overload _loc_ _typename_ _callback_)
// (%build_overload _loc_ _typename_)
void build_overload_impl(Context& C, ValueRefs Args) {
  if (Args.size() != 2 && Args.size() != 3)
    return C.RaiseError("invalid arity");

  mlir::OpBuilder* Builder = mlir_helper::getCurrentBuilder(C);
  if (!Builder)
    return;

  // Create the operation with an entry block with a single argument.
  schir::SourceLocation Loc = Args[0].getSourceLocation();
  schir::Value TypenameArg = Args[1];
  schir::Value Callback = Args.size() == 3 ? Args[2] : nullptr;

  if (!isa<String>(TypenameArg) && !isa<Symbol>(TypenameArg))
    return C.RaiseError("expecting string-like object for typename");

  llvm::StringRef Typename = TypenameArg.getStringRef();
  mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                             Builder->getContext());
  auto OverloadOp = nbdl_gen::OverloadOp::create(*Builder, MLoc, Typename);
  if (!Callback)
    return C.Cont();

  OverloadOp.getBody().emplaceBlock();
  assert(OverloadOp.getBody().hasOneBlock() && "expecting a single block");

  schir::Value Thunk = C.CreateLambda(
    [OverloadOp](Context& C, ValueRefs) mutable {
      schir::Value Callback = C.getCapture(0);
      mlir::Region& Body = OverloadOp.getBody();
      mlir::Location MLoc = OverloadOp.getLoc();
      mlir::Type Type = mlir::OpBuilder(OverloadOp)
        .getType<nbdl_gen::StoreType>();
      mlir::BlockArgument BlockArg = Body.addArgument(Type, MLoc);
      schir::Value V = C.CreateAny(mlir::Value(BlockArg));
      C.Apply(Callback, V);
    }, CaptureList{Callback});

  // Call the thunk with a Builder at the entry point.
  mlir::OpBuilder NewBuilder = mlir::OpBuilder(OverloadOp.getBody());
  mlir_helper::with_builder_impl(C, NewBuilder, Thunk);
}

void build_match_if_impl(Context& C, ValueRefs Args) {
  if (Args.size() != 5)
    return C.RaiseError("invalid arity");

  mlir::OpBuilder* Builder = mlir_helper::getCurrentBuilder(C);
  if (!Builder)
    return;

  // Create the operation with an entry block with a single argument.
  schir::SourceLocation Loc = Args[0].getSourceLocation();
  mlir::Value Input = any_cast<mlir::Value>(Args[1]);
  mlir::Value Pred = any_cast<mlir::Value>(Args[2]);
  schir::Value ThenThunk = Args[3];
  schir::Value ElseThunk = Args[4];

  if (!Pred || !Input)
    return C.RaiseError("expecting mlir.value");

  mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                             Builder->getContext());
  auto MatchIfOp = nbdl_gen::MatchIfOp::create(*Builder, MLoc, Input, Pred);
  MatchIfOp.getThenRegion().emplaceBlock();
  MatchIfOp.getElseRegion().emplaceBlock();

  C.PushCont([MatchIfOp](Context& C, ValueRefs Args) mutable {
    schir::Value ElseThunk = C.getCapture(0);
    mlir::OpBuilder ElseBuilder(MatchIfOp.getElseRegion());
    mlir_helper::with_builder_impl(C, ElseBuilder, ElseThunk);
  }, CaptureList{ElseThunk});

  mlir::OpBuilder ThenBuilder(MatchIfOp.getThenRegion());
  mlir_helper::with_builder_impl(C, ThenBuilder, ThenThunk);
}

// Translate a nbdl dialect operation to C++.
// (translate-cpp op port)
// The parameter `op` may be an mlir::Operation* or a StringLike
// which will be used to look up the name in the module.
// Currently the "port" has to be a tagged llvm::raw_ostream.
void translate_cpp(Context& C, ValueRefs Args) {
  if (Args.size() != 2 && Args.size() != 1)
    return C.RaiseError("invalid arity");
  auto* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    return C.RaiseError("expecting mlir.operation: {}", Args[0]);

  llvm::raw_ostream* OS = nullptr;

  using ResultTy = std::tuple<std::string,
                              schir::SourceLocationEncoding*,
                              mlir::Operation*>;
  auto Result = ResultTy();


  // Do not capture the emphemeral Any object.
  if (Args.size() == 2) {
    if (auto LWF = any_cast<LexerWriterFnRef>(Args[1])) {
      Result = nbdl_gen::translate_cpp(LWF, Op);
    } else if (auto* Raw = any_cast<::llvm::raw_ostream>(&Args[1])) {
      OS = Raw;
    } else {
      return C.RaiseError("expecting llvm::raw_ostream"
                          " or schir::LexerWriterFnRef");
    }
  } else {
    OS = &llvm::outs();
  }
  if (OS) {
    auto LexerWriter = [&OS](schir::SourceLocation, llvm::StringRef Buffer) {
      *OS << Buffer;
    };
    Result = nbdl_gen::translate_cpp(LexerWriter, Op);
  }

  auto& [ErrMsg, ErrLoc, Irritant] = Result;
  if (!ErrMsg.empty()) {
    schir::SourceLocation Loc(ErrLoc);
    schir::Error* Err = C.CreateError(Loc, ErrMsg,
        Irritant ? schir::Value(Irritant) : Value(Undefined()));
    return C.Raise(Err);
  }
  C.Cont();
}

// If the current block has a terminator, wrap the
// entire block in a nbdl.scope. This supports the
// convention that only terminators may perform an
// operation that may invalidate child stores.
void close_previous_scope(Context& C, ValueRefs Args) {
  if (Args.size() != 0)
    return C.RaiseError("invalid arity");
  mlir::OpBuilder* Builder = mlir_helper::getCurrentBuilder(C);
  if (!Builder)
    return;  // error is already raised by getCurrentBuilder
  mlir::Block* Block = Builder->getBlock();
  assert(Block && isa<mlir::func::FuncOp>(Block->getParentOp())
      && "expecting func insertion point");
  if (Block->empty() || !Block->back().hasTrait<mlir::OpTrait::IsTerminator>())
    return C.Cont();

  mlir::Location Loc = Block->back().getLoc();

  // Create new Region for ScopeOp.
  auto ScopeBody = std::make_unique<mlir::Region>();
  mlir::Block& NewBlock = ScopeBody->emplaceBlock();
  while (!Block->empty())
    Block->front().moveBefore(&NewBlock, NewBlock.end());
  mlir::Operation* ScopeOp = nbdl_gen::ScopeOp::create(
                              *Builder, Loc, std::move(ScopeBody));
  Builder->setInsertionPointAfter(ScopeOp);

  C.Cont();
}

void build_context_impl(Context& C, ValueRefs Args) {
  return C.RaiseError("deprecated");
#if 0
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");

  std::optional<mlir::OpBuilder> BuilderOpt = getModuleBuilder(C);
  if (!BuilderOpt)
    return;
  mlir::OpBuilder Builder = BuilderOpt.value();

  // This part is exactly like build_match_params_impl
  schir::SourceLocation Loc = Args[0].getSourceLocation();
  // Require a schir::Symbol so it has a source location.
  schir::Symbol* NameSym = dyn_cast<schir::Symbol>(Args[0]);
  llvm::StringRef Name = NameSym->getStringRef();
  schir::Value NumParamsVal = Args[1];
  schir::Value Callback = Args[2];

  int32_t NumParams = isa<schir::Int>(NumParamsVal)
      ? int32_t(cast<schir::Int>(NumParamsVal))
      : int32_t(-1);

  if (NumParams < 0)
    return C.RaiseError("expecting positive integer for num_params");
  if (!NameSym || Name.empty())
    return C.RaiseError("expecting function name (symbol literal)");

  mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                             Builder.getContext());
  // Create the ContextOp.
  auto ContextOp = Builder.create<nbdl_gen::ContextOp>(MLoc, Name);
  mlir::Block& EntryBlock = ContextOp.getBody().emplaceBlock();

  // Create the arguments.
  auto StoreTy = Builder.getType<nbdl_gen::StoreType>();
  for (int32_t i = 0; i < NumParams; i++)
    EntryBlock.addArgument(StoreTy, MLoc);

  schir::Value Thunk = C.CreateLambda([ContextOp](Context& C,
                                                  ValueRefs) mutable {
    schir::Value Callback = C.getCapture(0);
    llvm::SmallVector<schir::Value, 8> BlockArgs;
    assert(!ContextOp.getBody().empty() && "should have entry block");
    for (mlir::Value MVal : ContextOp.getBody().getArguments()) {
      schir::Value V = C.CreateAny(MVal);
      BlockArgs.push_back(V);
    }

    C.Apply(Callback, BlockArgs);
  }, CaptureList{Callback});

  // Call the thunk with a Builder at the entry point.
  Builder = mlir::OpBuilder(ContextOp.getBody());
  mlir_helper::with_builder_impl(C, Builder, Thunk);
#endif
}


// (%build-match-op name storeval keyval
void build_match_op_impl(Context& C, ValueRefs Args) {
  C.Cont();
}
} //  end namespace schir::nbdl_bind

extern "C" {
// initialize the module for run-time independent of the compiler
void SCHIR_NBDL_INIT(schir::Context& C) {
  C.DialectRegistry->insert<nbdl_gen::NbdlDialect>();

  // Assume that the MLIRContext cleans up ModuleOps.
  mlir::OpBuilder Builder(C.MLIRContext.get());
  mlir::Location Loc = Builder.getUnknownLoc();
  mlir::ModuleOp ModuleOp
    = mlir::ModuleOp::create(Builder, Loc, "nbdl_gen_module");

  schir::nbdl_bind_var::current_nbdl_module.set(C, ModuleOp.getOperation());
  schir::nbdl_bind_var::translate_cpp = schir::nbdl_bind::translate_cpp;
  schir::nbdl_bind_var::close_previous_scope
    = schir::nbdl_bind::close_previous_scope;
  schir::nbdl_bind_var::build_match_params_impl
    = schir::nbdl_bind::build_match_params_impl;
  schir::nbdl_bind_var::build_overload_impl
    = schir::nbdl_bind::build_overload_impl;
  schir::nbdl_bind_var::build_match_if_impl
    = schir::nbdl_bind::build_match_if_impl;
  schir::nbdl_bind_var::build_context_impl
    = schir::nbdl_bind::build_context_impl;
  schir::nbdl_bind_var::build_match_op_impl
    = schir::nbdl_bind::build_match_op_impl;
}

void SCHIR_NBDL_LOAD_MODULE(schir::Context& C) {
  SCHIR_NBDL_INIT(C);
  schir::initModuleNames(C, SCHIR_NBDL_LIB_STR, {
    {"current-nbdl-module", schir::nbdl_bind_var::current_nbdl_module.get(C)},
    {"translate-cpp", schir::nbdl_bind_var::translate_cpp},
    {"close-previous-scope",
                  schir::nbdl_bind_var::close_previous_scope},
    {"%build-match-params", schir::nbdl_bind_var::build_match_params_impl},
    {"%build-overload", schir::nbdl_bind_var::build_overload_impl},
    {"%build-match-if", schir::nbdl_bind_var::build_match_if_impl},
    {"%build-context", schir::nbdl_bind_var::build_context_impl},
    {"%build-match-op", schir::nbdl_bind_var::build_match_op_impl},
  });
}
}
