//===--- Mlir.cpp - Mlir binding syntax for SchirScheme ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines syntax mlir bindings for SchirScheme.
//
//===----------------------------------------------------------------------===//

#include <schir/Context.h>
#include <schir/Dialect.h>
#include <schir/Mlir.h>
#include <schir/MlirHelper.h>
#include <schir/OpGen.h>
#include <schir/Value.h>
#include <mlir/AsmParser/AsmParser.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Support/LogicalResult.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Casting.h>
#include <memory>

#define SCHIR_MLIR_VAR(NAME) ::schir::mlir_bind_var::NAME

// Manually provide mangled names as needed.
#define SCHIR_MLIR_VAR_STR__create_op_impl \
                        SCHIR_MLIR_LIB_(Vrm6Screatemi2Sop)
#define SCHIR_MLIR_VAR_STR_(NAME) SCHIR_MLIR_VAR_STR__##NAME
#define SCHIR_MLIR_VAR_STR(X) SCHIR_MLIR_VAR_STR_STR(SCHIR_MLIR_VAR_STR_(X))
#define SCHIR_MLIR_VAR_STR_STR(X) SCHIR_MLIR_VAR_STR_STR_STR(X)
#define SCHIR_MLIR_VAR_STR_STR_STR(X) #X

namespace schir::mlir_bind_var {
schir::ContextLocal   current_context;
schir::ContextLocal   current_builder;
schir::ExternSyntax<> create_op;
schir::ExternFunction create_op_impl;
schir::ExternFunction get_region;
schir::ExternFunction entry_block;
schir::ExternFunction add_argument;
schir::ExternFunction results;
schir::ExternFunction result;
schir::ExternFunction at_block_begin;
schir::ExternFunction at_block_end;
schir::ExternFunction at_block_terminator;
schir::ExternFunction block_op;
schir::ExternFunction set_insertion_point;
schir::ExternFunction set_insertion_after;
schir::ExternFunction type;
schir::ExternFunction function_type_impl;
schir::ExternFunction attr;
schir::ExternFunction type_attr;
schir::ExternFunction value_attr;
template <typename AttrTy>
schir::ExternFunction string_attr;
schir::ExternFunction with_new_context;
schir::ExternFunction with_builder;
schir::ExternFunction load_dialect;
schir::ExternFunction parent_op;
schir::ExternFunction op_next;
schir::ExternFunction verify;
schir::ExternFunction module_lookup;
schir::ExternFunction is_value;
}

using namespace schir::mlir_helper;

static mlir::Location getMlirLocation(schir::Context& C,
                                      schir::SourceLocation Loc,
                                      mlir::MLIRContext* MC) {
  if (!Loc.isValid())
    Loc = C.getLoc();
  return mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(), MC);
}

namespace schir::mlir_bind {
// Provide function to support create_op syntax.
// Require type checking as a precondition.
// (%create-op _name_
//             _loc_
//             _attrs_        : vector?
//             _operands_     : vector?
//             _regions_      : number?
//             _result_types_ : vector?
//             _successors_   : vector?)
void create_op_impl(Context& C, ValueRefs Args) {
  if (Args.size() != 7)
    return C.RaiseError("invalid arity");

  schir::SourceLocation Loc   = Args[1].getSourceLocation();
  schir::Vector* Attributes   = cast<schir::Vector>(Args[2]);
  schir::Vector* Operands     = cast<schir::Vector>(Args[3]);
  int NumRegions              = cast<schir::Int>(Args[4]);
  schir::Vector* ResultTypes  = cast<schir::Vector>(Args[5]);
  schir::Vector* Successors   = cast<schir::Vector>(Args[6]);

  llvm::StringRef OpName = Args[0].getStringRef();
  if (OpName.empty())
    return C.RaiseError("expecting operation name: {}", Args[0]);

  Args = Args.drop_front();

  mlir::OpBuilder* Builder = getCurrentBuilder(C);
  if (!Builder) return;

  mlir::Location MLoc = getMlirLocation(C, Loc, Builder->getContext());
  auto OpState = mlir::OperationState(MLoc, OpName);

  // attributes
  for (schir::Value V : Attributes->getElements()) {
    llvm::StringRef Name;
    if (auto* P = dyn_cast<schir::Pair>(V)) {
      Name = P->Car.getStringRef();
      if (Name.empty())
        return C.RaiseError(
            "expecting name-value pair for attribute: {}", Value(P));
      if (auto* P2 = dyn_cast<schir::Pair>(P->Cdr)) {
        V = P2->Car;
        if (!isa<schir::Empty>(P2->Cdr))
          Name = "";  // Clear the name so we raise error below.
      }
    }

    auto Attr = any_cast<mlir::Attribute>(V);

    // If the object is not a mlir::Attribute, simply make
    // the schir::Value into one.
    if (!Attr)
      Attr = SchirValueAttr::get(Builder->getContext(), V);

    mlir::NamedAttribute NamedAttr = Builder->getNamedAttr(Name, Attr);
    OpState.attributes.push_back(NamedAttr);
  }

  // operands
  for (schir::Value V : Operands->getElements()) {
    // Implicitly unwrap lists.
    if (isa<schir::Pair, schir::Empty>(V)) {
      for (schir::Value V2 : V) {
        auto MVal = any_cast<mlir::Value>(V2);
        if (!MVal)
          return C.RaiseError("expecting mlir.value: {}", V2);
        OpState.operands.push_back(MVal);
      }
      break;
    }
    auto MVal = any_cast<mlir::Value>(V);
    if (!MVal)
      return C.RaiseError("expecting mlir.value: {}", V);
    OpState.operands.push_back(MVal);
  }

  // regions
  for (int I = 0; I < NumRegions; ++I)
    OpState.regions.push_back(std::make_unique<mlir::Region>());

  // result-types
  for (schir::Value V : ResultTypes->getElements()) {
    auto MType = any_cast<mlir::Type>(V);
    if (!MType)
      return C.RaiseError("expecting mlir.type: {}", V);
    OpState.types.push_back(MType);
  }

  // successors
  for (schir::Value V : Successors->getElements()) {
    mlir::Block* Block = any_cast<mlir::Block*>(V);
    if (Block == nullptr)
      return C.RaiseError("expecting mlir.block: {}", V);
    OpState.successors.push_back(Block);
  }

  // Create the operation using the Builder
  mlir::Operation* Op = Builder->create(OpState);
  C.Cont(schir::Value(Op));
}

// Create operation syntax. Argument are
//  specified by any of the following auxilary keywords:
//    attributes - takes list of name-value pairs
//                 where the value is either mlir.attr or
//                 schir::Value is lifted to mlir.attr.
//    operands - takes list of mlir.values
//    regions - takes a single integer for the number of regions
//    result-types - takes list of mlir.types
//    successors - takes a list of mlir.blocks
//  (create-op _name_
//    (attributes   _attrs_ ...)
//    (operands     _values_ ...)
//    (regions      _regions_ )
//    (result-types _types_ ...)
//    (successors   _blocks_ ...))
//
void create_op(Context& C, ValueRefs Args) {  // Syntax
  schir::SourceLocation CallLoc = cast<Pair>(Args[0])
      ->Car.getSourceLocation();
  schir::OpGen& OpGen = *C.OpGen;
  schir::Value SpecifiedLoc = schir::Empty();
  schir::Value Attributes   = schir::Empty();
  schir::Value Operands     = schir::Empty();
  schir::Value NumRegions   = schir::Int(0);
  schir::Value ResultTypes  = schir::Empty();
  schir::Value Successors   = schir::Empty();

  schir::Pair* Input = dyn_cast<Pair>(cast<Pair>(Args[0])->Cdr);
  if (!Input)
    return C.RaiseError("expecting arguments");

  // Note that calls to GetSingleResult handle TailPos for us.

  // Require the _name_ argument.
  mlir::Value OpName = OpGen.GetSingleResult(Input->Car);
  if (OpGen.CheckError())
    return;

  // Process named arguments which are optional.
  for (auto [Loc, Arg] : WithSource(Input->Cdr)) {
    llvm::StringRef ArgName;
    if (schir::Pair* P = dyn_cast<Pair>(Arg)) {
      ArgName = P->Car.getStringRef();
      Arg = P->Cdr;
    }

    if (ArgName == "loc") {
      SpecifiedLoc = Arg;
      // Support improper list.
      if (auto* PArg = dyn_cast<Pair>(Arg))
        SpecifiedLoc = PArg->Car;
      else
        SpecifiedLoc = Arg;
    } else if (ArgName == "attributes") {
      Attributes = Arg;
    } else if (ArgName == "operands") {
      Operands = Arg;
    } else if (ArgName == "regions") {
      // Support improper list.
      if (auto* PArg = dyn_cast<Pair>(Arg))
        NumRegions = PArg->Car;
      else
        NumRegions = Arg;
    } else if (ArgName == "result-types") {
      ResultTypes = Arg;
    } else if (ArgName == "successors") {
      Successors = Arg;
    } else {
      return C.RaiseError("expecting named argument "
          "[attributes, operands, regions, result-types, successors]");
    }
  }

  llvm::SmallVector<mlir::Value, 5> Vals;
  auto createInputVector = [&](schir::Value Inputs) -> mlir::Value {
    for (auto [Loc, X] : WithSource(Inputs)) {
      C.setLoc(Loc);
      mlir::Value V = OpGen.GetSingleResult(X);
      if (OpGen.CheckError())
        return mlir::Value();
      Vals.push_back(V);
    }

    // Localize the values because we are doing this manually.
    for (auto& V : Vals)
      V = OpGen.LocalizeValue(V);

    mlir::Value InputVals = OpGen.create<schir::VectorOp>(
                              Attributes.getSourceLocation(), Vals);
    Vals.clear();
    return InputVals;
  };

  mlir::Value AttrVals = createInputVector(Attributes);
  mlir::Value OperandVals = createInputVector(Operands);
  mlir::Value SpecifiedLocVal = OpGen.GetSingleResult(SpecifiedLoc);
  mlir::Value NumRegionsVal = OpGen.GetSingleResult(NumRegions);
  if (OpGen.CheckError())
    return;
  mlir::Value ResultTypeVals = createInputVector(ResultTypes);
  mlir::Value SuccessorVals = createInputVector(Successors);

  assert(Vals.empty());
  // Reuse Vals as arguments to %create-op
  Vals.push_back(OpName);
  Vals.push_back(SpecifiedLocVal);
  Vals.push_back(AttrVals);
  Vals.push_back(OperandVals);
  Vals.push_back(NumRegionsVal);
  Vals.push_back(ResultTypeVals);
  Vals.push_back(SuccessorVals);

  // Create the call to %create-op (aka create_op_impl)
  mlir::Value Fn = OpGen.create<LoadGlobalOp>(CallLoc,
                        SCHIR_MLIR_VAR_STR(create_op_impl));
  mlir::Value Call = OpGen.createCall(CallLoc, Fn, Vals);
  C.Cont(OpGen.fromValue(Call));
}

// Get an operation get_region by index (defaulting to 0).
// (get-region _op_)
// (get-region op _index_)
void get_region(Context& C, ValueRefs Args) {
  if (Args.size() != 1 && Args.size() != 2)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = schir::dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    return C.RaiseError("expecting mlir.op: {}", {Args[0]});

  if (Args.size() > 1 && !schir::isa<schir::Int>(Args[1]))
    return C.RaiseError("expecting index: {}", {Args[1]});

  int32_t Index = Args.size() > 1 && schir::isa<schir::Int>(Args[1])
                    ? int32_t{schir::cast<schir::Int>(Args[1])} : 0;
  // Regions are part of the Ops TrailingObjects so
  // we can expect the pointers to be stable.
  mlir::Region* Region = &(Op->getRegion(Index));
  if (!Region)
    return C.RaiseError("invalid mlir.region");
  C.Cont(C.CreateAny(Region));
}

// Get entry block from region/op by index.
// If an op is provided the first region is used.
// Add block to region if empty.
void entry_block(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  mlir::Region* Region = nullptr;
  if (mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0])) {
    if (Op->getNumRegions() < 1)
      return C.RaiseError("mlir.op has no regions");
    Region = &Op->getRegion(0);
  } else {
    Region = any_cast<mlir::Region*>(Args[0]);
  }

  if (!Region)
    return C.RaiseError("expecting mlir.op/mlir.region: {}", Args[0]);
  if (Region->empty())
    Region->emplaceBlock();
  mlir::Block* Block = &(Region->front());
  if (!Block)
    return C.RaiseError("invalid mlir.block");
  C.Cont(C.CreateAny(Block));
}

// (add-argument block type loc)
void add_argument(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");
  mlir::Block* Block = any_cast<mlir::Block*>(Args[0]);
  mlir::Type Type = any_cast<mlir::Type>(Args[1]);
  schir::SourceLocation Loc= Args[2].getSourceLocation();
  if (!Block)
    return C.RaiseError("expecting mlir.block: {}", Args[0]);
  if (!Type)
    return C.RaiseError("expecting mlir.type: {}", Args[1]);

  mlir::OpBuilder* Builder = getCurrentBuilder(C);
  if (!Builder)
    return;

  mlir::Location MLoc = getMlirLocation(C, Loc, Builder->getContext());
  mlir::BlockArgument BA = Block->addArgument(Type, MLoc);
  schir::Value Result = C.CreateAny(mlir::Value(BA));
  C.Cont(Result);
}

// Get list of results of op.
void results(Context& C, ValueRefs Args) {
  // This might be useful for applying to operations
  // via quasiquote splicing.
  C.RaiseError("TODO not implemented");
}

// Get operation result by index (default = 0).
// (result _op_)
// (result _op_ _index_)
void result(Context& C, ValueRefs Args) {
  if (Args.size() != 1 && Args.size() != 2)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = schir::dyn_cast<mlir::Operation>(Args[0]);
  Value IndexArg = Args.size() > 1 ? Args[1] : Value(Int{0});

  if (!Op)
    return C.RaiseError("expecting mlir.op, {}", Args[0]);

  if (!schir::isa<schir::Int>(IndexArg))
    return C.RaiseError("expecting index: {}", IndexArg);

  uint32_t Index = static_cast<uint32_t>(schir::cast<schir::Int>(IndexArg));

  if (Index >= Op->getNumResults())
    return C.RaiseError("result index is out of range");

  mlir::Value Result = Op->getResult(Index);
  if (!Result)
    return C.RaiseError("invalid mlir.op result");
  C.Cont(C.CreateAny(Result));
}

// Get the nth block argument from a block.
// (block-arg _block_ _index_)
void block_arg(Context& C, ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  mlir::Block* Block = any_cast<mlir::Block*>(Args[0]);
  if (!Block || !isa<schir::Int>(Args[1]))
    return C.RaiseError("expecting block and index: {} and {}", Args);
  int Index = cast<schir::Int>(Args[1]);
  // A mlir::BlockArgument is a mlir::Value
  mlir::Value Val = Block->getArgument(Index);
  schir::Value MVal = C.CreateAny(Val);
  C.Cont(MVal);
}

// Copy the builder. (ie Do not modify.)
// (with-builder [_builder_] _thunk_)
void with_builder(Context& C, ValueRefs Args) {
  if (Args.empty() || Args.size() > 2)
    return C.RaiseError("expecting 2 arguments");

  schir::Value Thunk;
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  mlir::OpBuilder Builder(MLIRContext);
  if (Args.size() == 2) {
    mlir::OpBuilder* BuilderPtr = getBuilder(C, Args[0]);
    if (!BuilderPtr)
      return;
    Thunk = Args[1];
    Builder = *BuilderPtr;
  } else {
    Thunk = Args[0];
  }

  return with_builder_impl(C, Builder, Thunk);
}

static mlir::Block* get_arg_block(Context& C, ValueRefs Args) {
  mlir::Block* Block = nullptr;
  if (Args.size() == 1)
    Block = any_cast<mlir::Block*>(Args[0]);
  if (Block == nullptr)
    C.RaiseError("expecting mlir.block: {}", Args[0]);
  return Block;
}

// Alter current-builder to insert at beginning of block.
// (at-block-begin _block_)
void at_block_begin(Context& C, ValueRefs Args) {
  if (mlir::Block* Block = get_arg_block(C, Args)) {
    mlir::OpBuilder* Builder = getCurrentBuilder(C);
    if (!Builder) return;
    *Builder = mlir::OpBuilder::atBlockBegin(Block);
    C.Cont();
  }
  // Note: error raised in get_arg_block
}

// Alter current-builder to insert at end to block
// (at-block-end _block_)
void at_block_end(Context& C, ValueRefs Args) {
  if (mlir::Block* Block = get_arg_block(C, Args)) {
    mlir::OpBuilder* Builder = getCurrentBuilder(C);
    if (!Builder) return;
    *Builder = mlir::OpBuilder::atBlockEnd(Block);
    C.Cont();
  }
  // Note: error raised in get_arg_block
}

// Alter current-builder to insert before terminator in block.
// (at-block-terminator _block_)
void at_block_terminator(Context& C, ValueRefs Args) {
  if (mlir::Block* Block = get_arg_block(C, Args)) {
    mlir::OpBuilder* Builder = getCurrentBuilder(C);
    if (!Builder) return;
    *Builder = mlir::OpBuilder::atBlockTerminator(Block);
    C.Cont();
  }
  // Note: error raised in get_arg_block
}

// Set insertion point to prepend. (alters current-builder)
// (set-insertion-point _op_or_region_)
// Argument can be Operation, Region
//  Operation - Insert before operation in containing block.
//  Region    - Insert before any operation in first block.
void set_insertion_point(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  mlir::OpBuilder* Builder = getCurrentBuilder(C);
  if (!Builder) return;

  if (auto* Op = dyn_cast<mlir::Operation>(Args[0])) {
    // Insert before the operation.
    Builder->setInsertionPoint(Op);
  } else if (mlir::Region* R = any_cast<mlir::Region*>(Args[0])) {
    // Automatically add block if needed.
    if (R->empty())
      R->emplaceBlock();
    mlir::Block* Block = &R->front();
    Builder->setInsertionPointToStart(Block);
  } else {
    return C.RaiseError("expecting mlir.op or mlir.region: {}", Args[0]);
  }
  C.Cont();
}

// Set insertion point to after the operation. (alters current-builder)
// (set-insertion-after _op_)
void set_insertion_after(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::OpBuilder* Builder = getCurrentBuilder(C);
  if (!Builder) return;

  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    return C.RaiseError("expecting mlir_operation: {}", Args[0]);

  Builder->setInsertionPoint(Op);
  C.Cont();
}

// Get a type by parsing a string.
// (type _string_)
void type(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  llvm::StringRef TypeStr = Args[0].getStringRef();
  if (TypeStr.empty())
    return C.RaiseError("expecting string with type:", Args[0]);

  mlir::Type Type = mlir::parseType(TypeStr, MLIRContext,
                                    nullptr, schir::String::IsNullTerminated);
  if (!Type)
    return C.RaiseError("mlir type parse failed");

  C.Cont(C.CreateAny(Type));
}

// Create a function type (using vector literals.
// (%function-type #(<arg-types>...) #(<result-types>...))
void function_type_impl(Context& C, ValueRefs Args) {
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  auto* ArgTypeVals = schir::dyn_cast<schir::Vector>(Args[0]);
  auto* ResultTypeVals = schir::dyn_cast<schir::Vector>(Args[1]);
  if (!ArgTypeVals || !ResultTypeVals)
    return C.RaiseError("expecting vectors: {} and {}", Args);

  // Arg types
  llvm::SmallVector<mlir::Type, 8> ArgTypes;
  Args = Args.drop_front();
  for (schir::Value Arg : ArgTypeVals->getElements()) {
    mlir::Type ArgType = any_cast<mlir::Type>(Arg);
    if (!ArgType)
      return C.RaiseError("expecting mlir.type: {}", Arg);
    ArgTypes.push_back(ArgType);
  }

  // Result types
  llvm::SmallVector<mlir::Type, 8> ResultTypes;
  for (schir::Value Result : ResultTypeVals->getElements()) {
    mlir::Type ResultType = any_cast<mlir::Type>(Result);
    if (!ResultType)
      return C.RaiseError("expecting mlir.type: {}", Result);
    ResultTypes.push_back(ResultType);
  }

  mlir::Type Type = mlir::FunctionType::get(MLIRContext, ArgTypes,
                                            ResultTypes);

  if (!Type)
    return C.RaiseError("mlir build function type failed");

  C.Cont(C.CreateAny(Type));
}

// Get an attribute by parsing a string.
//  Usage: (attr _attr_str [_type_])
//    attr_str - the string to be parsed
//    type - a string or a mlir.type object (defaults to NoneType)
void attr(Context& C, ValueRefs Args) {
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  mlir::Attribute Attr;
  if (Args.size() > 2 || Args.empty())
    return C.RaiseError("invalid arity");

  schir::Value AttrStrArg = Args[0];

  mlir::Type Type;
  if (Args.size() == 2) {
    schir::Value TypeArg = Args[1];
    llvm::StringRef TypeStr = TypeArg.getStringRef();
    if (!TypeStr.empty()) {
      Type = mlir::parseType(TypeStr, MLIRContext, nullptr,
                             schir::String::IsNullTerminated);
      if (!Type)
        return C.RaiseError("mlir type parse failed");
    }
    else {
      Type = any_cast<mlir::Type>(TypeArg);
      if (!Type)
        return C.RaiseError("invalid mlir type");
    }
  }
  if (!Type)
    Type = mlir::NoneType::get(MLIRContext);

  llvm::StringRef AttrStr = AttrStrArg.getStringRef();
  if (AttrStr.empty())
    return C.RaiseError("expecting string: {}", AttrStrArg);

  Attr = mlir::parseAttribute(AttrStr, MLIRContext,
                              Type, nullptr,
                              schir::String::IsNullTerminated);
  if (!Attr)
    return C.RaiseError("mlir attribute parse failed");

  C.Cont(C.CreateAny(Attr));
}

// (type-attr (type "!type-goes-here"))
void type_attr(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::Type Type = any_cast<mlir::Type>(Args[0]);
  if (!Type)
    return C.RaiseError("expecting a mlir.type: {}", Args[0]);
  mlir::Attribute Attr = mlir::TypeAttr::get(Type);
  C.Cont(C.CreateAny(Attr));
}

// Create a schir scheme value attribute of type !schir.value
void value_attr(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  mlir::Attribute Attr = SchirValueAttr::get(MLIRContext, Args[0]);
  C.Cont(C.CreateAny(Attr));
}

template <typename AttrTy>
void string_attr(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (!isa<schir::String, schir::Symbol>(Args[0]))
    return C.RaiseError("expecting string-like object: {}", Args[0]);
  llvm::StringRef Str = Args[0].getStringRef();
  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  mlir::Attribute Attr = AttrTy::get(MLIRContext, Str);
  C.Cont(C.CreateAny(Attr));
}

// (with-new-context _thunk_)
void with_new_context(schir::Context& C, schir::ValueRefs Args) {
  // Create a new context, and call a
  // thunk with it as the current-context.
  if (Args.size() != 1)
    return C.RaiseError("expecting single thunk");

  auto* Thunk = dyn_cast<schir::Lambda>(Args[0]);

  if (!Thunk)
    return C.RaiseError("expecting thunk: {}", Args[0]);

  auto NewContextPtr = std::make_unique<mlir::MLIRContext>(*C.DialectRegistry);
  schir::Value NewMC = C.CreateAny(NewContextPtr.get());
  schir::Value NewBuilder = C.CreateAny(mlir::OpBuilder(NewContextPtr.get()));

  // The Prev values are Bindings because we can enter
  // this via escape procedure from anywhere.
  // (If that was not obvious)
  schir::Value PrevMC = C.CreateBinding(schir::Empty());
  schir::Value PrevBuilder = C.CreateBinding(schir::Empty());

  schir::Value Before = C.CreateLambda(
    [](schir::Context& C, schir::ValueRefs Args) {
      // Save the previous state and instate the new... state.
      // (ie MLIRContext and Builder)
      auto* PrevMC = cast<schir::Binding>(C.getCapture(0));
      auto* PrevBuilder = cast<schir::Binding>(C.getCapture(1));
      schir::Value NewMC = C.getCapture(2);
      schir::Value NewBuilder = C.getCapture(3);

      // Set the Bindings
      PrevMC->setValue(SCHIR_MLIR_VAR(current_context).get(C));
      PrevBuilder->setValue(SCHIR_MLIR_VAR(current_builder).get(C));

      // Set the "current" values
      SCHIR_MLIR_VAR(current_context).set(C, NewMC);
      SCHIR_MLIR_VAR(current_builder).set(C, NewBuilder);
      C.Cont();
    }, CaptureList{PrevMC, PrevBuilder, NewMC, NewBuilder});

  schir::Value After = C.CreateLambda(
    [](schir::Context& C, schir::ValueRefs Args) {
      // Restore previous state
      auto* PrevMC = cast<schir::Binding>(C.getCapture(0));
      auto* PrevBuilder = cast<schir::Binding>(C.getCapture(1));
      SCHIR_MLIR_VAR(current_context).set(C, PrevMC->getValue());
      SCHIR_MLIR_VAR(current_builder).set(C, PrevBuilder->getValue());
      C.Cont();
    }, CaptureList{PrevMC, PrevBuilder});

  C.DynamicWind(std::move(NewContextPtr), Before, Thunk, After);
}

// Dynamically load a mlir dialect by its name.
// (load-dialect _name_)
void load_dialect(Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("expecting dialect name");

  llvm::StringRef Name = Args[0].getStringRef();

  if (Name.empty())
    return C.RaiseError("expecting dialect name");

  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  // Ensure the registry is up to date.
  MLIRContext->appendDialectRegistry(*C.DialectRegistry);
  mlir::Dialect* Dialect = MLIRContext->getOrLoadDialect(Name);
  if (Dialect == nullptr)
    return C.RaiseError(C.CreateString("failed to load dialect: ", Name), {});

  C.Cont();
}

// Get parent operation of block.
// (block-op _block_)
void block_op(Context& C, schir::ValueRefs Args) {
  mlir::Block* Block = nullptr;
  if (Args.size() == 1)
    Block = any_cast<mlir::Block*>(Args[0]);

  if (!Block)
    return C.RaiseError("expecting block: {}", Args);

  mlir::Operation* ParentOp = Block->getParentOp();
  schir::Value Result = ParentOp != nullptr ? schir::Value(ParentOp) :
                                              schir::Empty();
  C.Cont(Result);
}

// Get the next operation from an operation
// or () if we find the tail of the list.
// (op-parent _op_)
void op_next(Context& C, schir::ValueRefs Args) {
  if (mlir::Operation* Op = getSingleOpArg(C, Args)) {
    Op = Op->getNextNode();
    schir::Value Result = Op != nullptr ? schir::Value(Op) :
                                          schir::Empty();
    C.Cont(Result);
  }
}

// Get the parent operation of an operation.
// or () if the operation is unlinked
// (parent-op _op_)
void parent_op(Context& C, schir::ValueRefs Args) {
  if (mlir::Operation* Op = getSingleOpArg(C, Args)) {
    Op = Op->getParentOp();
    schir::Value Result = Op != nullptr ? schir::Value(Op) :
                                          schir::Empty();
    C.Cont(Result);
  }
}

// Return true if operation verify is success otherwise
// false. Diagnostics are output to stderr by default via MLIR.
// (verify _op_)
void verify(Context& C, schir::ValueRefs Args) {
  if (mlir::Operation* Op = getSingleOpArg(C, Args)) {
    bool Success = mlir::verify(Op).succeeded();
    C.Cont(schir::Bool(Success));
  }
}

// (symbol-table-lookup _module-op_ _"symbolname"_)
void module_lookup(Context& C, schir::ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");

  mlir::ModuleOp ModuleOp = dyn_cast_or_null<mlir::ModuleOp>(
      dyn_cast<mlir::Operation>(Args[0]));
  llvm::StringRef SymbolName = Args[1].getStringRef();
  if (!ModuleOp)
    return C.RaiseError("expecting mlir::ModuleOp: {}", Args[0]);
  if (SymbolName.empty())
    return C.RaiseError("expecting nonempty string-like object: {}", Args[1]);

  mlir::Operation* Op = ModuleOp.lookupSymbol(SymbolName);
  schir::Value Result = Op != nullptr ? schir::Value(Op) : schir::Empty();
  C.Cont(Result);
}

// (value? obj)
// (value? _typestr_ obj)
// (value? mlir.type obj)
void is_value(Context& C, ValueRefs Args) {
  if (Args.size() < 1 || Args.size() > 2)
    return C.RaiseError("invalid arity");

  mlir::MLIRContext* MLIRContext = getCurrentContext(C);
  mlir::Type Type;

  if (Args.size() == 2) {
    if (isa<String, Symbol>(Args[0])) {
      llvm::StringRef TypeStr = Args[0].getStringRef();
      Type = mlir::parseType(TypeStr, MLIRContext,
                             nullptr, schir::String::IsNullTerminated);
    } else {
      Type = any_cast<mlir::Type>(Args[0]);
    }

    if (!Type)
      return C.RaiseError("expecting a mlir.type: {}", Args[0]);
    Args = Args.drop_front();
  }

  mlir::Value Value = any_cast<mlir::Value>(Args[0]);
  bool Result = Value && (!Type || Value.getType() == Type);
  C.Cont(schir::Bool(Result));
}

}  // namespace schir::mlir_bind

extern "C" {
// initialize the module for run-time independent of the compiler
void SCHIR_MLIR_INIT(schir::Context& C) {
  mlir::MLIRContext* MC = C.MLIRContext.get();
  schir::Value MC_Val = C.CreateAny(MC);
  schir::Value BuilderVal = C.CreateAny(mlir::OpBuilder(MC));
  SCHIR_MLIR_VAR(current_context).set(C, MC_Val);
  SCHIR_MLIR_VAR(current_builder).set(C, BuilderVal);

  SCHIR_MLIR_VAR(create_op) = schir::mlir_bind::create_op;
  SCHIR_MLIR_VAR(create_op_impl) = schir::mlir_bind::create_op_impl;
  SCHIR_MLIR_VAR(get_region) = schir::mlir_bind::get_region;
  SCHIR_MLIR_VAR(entry_block) = schir::mlir_bind::entry_block;
  SCHIR_MLIR_VAR(add_argument) = schir::mlir_bind::add_argument;
  SCHIR_MLIR_VAR(results) = schir::mlir_bind::results;
  SCHIR_MLIR_VAR(result) = schir::mlir_bind::result;
  SCHIR_MLIR_VAR(at_block_begin) = schir::mlir_bind::at_block_begin;
  SCHIR_MLIR_VAR(at_block_end) = schir::mlir_bind::at_block_end;
  SCHIR_MLIR_VAR(block_op) = schir::mlir_bind::block_op;
  SCHIR_MLIR_VAR(op_next) = schir::mlir_bind::op_next;
  SCHIR_MLIR_VAR(parent_op) = schir::mlir_bind::parent_op;
  SCHIR_MLIR_VAR(set_insertion_point) = schir::mlir_bind::set_insertion_point;
  SCHIR_MLIR_VAR(set_insertion_after) = schir::mlir_bind::set_insertion_after;
  SCHIR_MLIR_VAR(type) = schir::mlir_bind::type;
  SCHIR_MLIR_VAR(function_type_impl) = schir::mlir_bind::function_type_impl;
  SCHIR_MLIR_VAR(attr) = schir::mlir_bind::attr;
  SCHIR_MLIR_VAR(type_attr) = schir::mlir_bind::type_attr;
  SCHIR_MLIR_VAR(value_attr) = schir::mlir_bind::value_attr;
  SCHIR_MLIR_VAR(string_attr<mlir::StringAttr>)
    = schir::mlir_bind::string_attr<mlir::StringAttr>;
  SCHIR_MLIR_VAR(string_attr<mlir::FlatSymbolRefAttr>)
    = schir::mlir_bind::string_attr<mlir::FlatSymbolRefAttr>;
  SCHIR_MLIR_VAR(load_dialect) = schir::mlir_bind::load_dialect;
  SCHIR_MLIR_VAR(with_builder) = schir::mlir_bind::with_builder;
  SCHIR_MLIR_VAR(with_new_context) = schir::mlir_bind::with_new_context;
  SCHIR_MLIR_VAR(verify) = schir::mlir_bind::verify;
  SCHIR_MLIR_VAR(module_lookup) = schir::mlir_bind::module_lookup;
  SCHIR_MLIR_VAR(is_value) = schir::mlir_bind::is_value;
}

void SCHIR_MLIR_LOAD_MODULE(schir::Context& C) {
  SCHIR_MLIR_INIT(C);
  schir::initModuleNames(C, SCHIR_MLIR_LIB_STR, {
    {"old-create-op", SCHIR_MLIR_VAR(create_op)},
    {"%create-op", SCHIR_MLIR_VAR(create_op_impl)},
    {"current-builder", SCHIR_MLIR_VAR(current_builder).getBinding(C)},
    {"get-region", SCHIR_MLIR_VAR(get_region)},
    {"entry-block", SCHIR_MLIR_VAR(entry_block)},
    {"add-argument", SCHIR_MLIR_VAR(add_argument)},
    {"results", SCHIR_MLIR_VAR(results)},
    {"result", SCHIR_MLIR_VAR(result)},
    {"at-block-begin", SCHIR_MLIR_VAR(at_block_begin)},
    {"at-block-end", SCHIR_MLIR_VAR(at_block_end)},
    {"block-op", SCHIR_MLIR_VAR(block_op)},
    {"op-next", SCHIR_MLIR_VAR(op_next)},
    {"parent-op", SCHIR_MLIR_VAR(parent_op)},
    {"set-insertion-point", SCHIR_MLIR_VAR(set_insertion_point)},
    {"set-insertion-after", SCHIR_MLIR_VAR(set_insertion_after)},
    {"type", SCHIR_MLIR_VAR(type)},
    {"%function-type", SCHIR_MLIR_VAR(function_type_impl)},
    {"attr", SCHIR_MLIR_VAR(attr)},
    {"type-attr", SCHIR_MLIR_VAR(type_attr)},
    {"value-attr", SCHIR_MLIR_VAR(value_attr)},
    {"string-attr", SCHIR_MLIR_VAR(string_attr<mlir::StringAttr>)},
    {"flat-symbolref-attr",
      SCHIR_MLIR_VAR(string_attr<mlir::FlatSymbolRefAttr>)},
    {"with-new-context", SCHIR_MLIR_VAR(with_new_context)},
    {"with-builder", SCHIR_MLIR_VAR(with_builder)},
    {"load-dialect", SCHIR_MLIR_VAR(load_dialect)},
    {"verify", SCHIR_MLIR_VAR(verify)},
    {"module-lookup", SCHIR_MLIR_VAR(module_lookup)},
    {"value?", SCHIR_MLIR_VAR(is_value)},
  });
}
}
