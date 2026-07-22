#include <nbdl_spec/NbdlDialect.h>
#include <schir/Context.h>
#include <schir/Value.h>
#include <schir/MlirHelper.h>
#include <llvm/Support/Casting.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <memory>
#include <optional>
#include <tuple>

using Context = schir::Context;
using ValueRefs = schir::ValueRefs;
using CaptureList = schir::CaptureList;
namespace mlir_helper = schir::mlir_helper;
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;
using llvm::isa_and_nonnull;

namespace nbdl_spec {
extern std::tuple<std::string, schir::SourceLocationEncoding*, mlir::Operation*>
translate_cpp(schir::LexerWriterFnRef FnRef, mlir::Operation* Op);
}

extern "C" {
// Create a function and call the thunk with a new builder
// to insert into the function body.
// _num_store_params_ N
// _callback_ takes _num_store_params_ + 1 arguments which are the block arguments
//  with formals like (store1 store2 ... storeN fn)
// (%build_match_params _name_ _num_store_params_ _callback_)
void nbdl_spec_build_match_params(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");

  mlir::OpBuilder* Builder = mlir_helper::getCurrentBuilder(C);
  if (!Builder)
    return;  // error is already raised by getCurrentBuilder

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
                                             Builder->getContext());

  // Create the function type.
  llvm::SmallVector<mlir::Type, 8> InputTypes;
  for (unsigned i = 0; i < static_cast<uint32_t>(NumParams); i++) {
    mlir::Type StoreT = nbdl_spec::StoreType::get(Builder->getContext());
    InputTypes.push_back(StoreT);
  }

  // Push the visitor fn argument.
  mlir::Type StoreT = nbdl_spec::StoreType::get(Builder->getContext());
  InputTypes.push_back(StoreT);

  mlir::FunctionType FT = Builder->getFunctionType(InputTypes,
                                                  /*ResultTypes*/{});

  // Create the function.
  auto FuncOp = mlir::func::FuncOp::create(*Builder, MLoc, Name, FT);
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

  // Call the thunk with a NewBuilder at the entry point.
  auto NewBuilder = mlir::OpBuilder(FuncOp.getBody());
  mlir_helper::WithBuilderImpl(C, NewBuilder, Thunk);
}

// Translate a nbdl dialect operation to C++.
// (translate-cpp op port)
// The parameter `op` may be an mlir::Operation* or a StringLike
// which will be used to look up the name in the module.
// Currently the "port" has to be a tagged llvm::raw_ostream.
void nbdl_spec_translate_cpp(Context& C, ValueRefs Args) {
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
    if (auto LWF = schir::any_cast<schir::LexerWriterFnRef>(Args[1])) {
      Result = nbdl_spec::translate_cpp(LWF, Op);
    } else if (auto* Raw = schir::any_cast<::llvm::raw_ostream>(&Args[1])) {
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
    Result = nbdl_spec::translate_cpp(LexerWriter, Op);
  }

  auto& [ErrMsg, ErrLoc, Irritant] = Result;
  if (!ErrMsg.empty()) {
    schir::SourceLocation Loc(ErrLoc);
    schir::Error* Err = C.CreateError(Loc, ErrMsg,
        Irritant ? schir::Value(Irritant) : schir::Value(schir::Undefined()));
    return C.Raise(Err);
  }
  C.Cont();
}

// If the current block has a terminator, wrap the
// entire block in a nbdl.scope. This supports the
// convention that only terminators may perform an
// operation that may invalidate child stores.
void nbdl_spec_close_previous_scope(Context& C, ValueRefs Args) {
  if (Args.size() != 0)
    return C.RaiseError("invalid arity");
  mlir::OpBuilder* Builder = mlir_helper::getCurrentBuilder(C);
  if (!Builder)
    return;  // error is already raised by getCurrentBuilder
  mlir::Block* Block = Builder->getBlock();
  if (Block->empty() || !Block->back().hasTrait<mlir::OpTrait::IsTerminator>())
    return C.Cont();

  mlir::Location Loc = Block->back().getLoc();

  // Create new Region for ScopeOp.
  auto ScopeBody = std::make_unique<mlir::Region>();
  mlir::Block& NewBlock = ScopeBody->emplaceBlock();
  while (!Block->empty())
    Block->front().moveBefore(&NewBlock, NewBlock.end());
  mlir::Operation* ScopeOp
    = nbdl_spec::ScopeOp::create(*Builder, Loc, std::move(ScopeBody));
  Builder->setInsertionPointAfter(ScopeOp);

  C.Cont();
}

// TODO Since top level ModuleOps are not owned by MLIRContext,
//      we need to use std::shared_ptr here once support for it
//      is added in Schir.
// Return a new top level mlir module.
void nbdl_spec_create_top_module(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  llvm::StringRef Name = Args.front().getStringRef();
  if (Name.empty())
    return C.RaiseError("module name should be non-empty string-like: {}",
                        Args.front());

  // Register the nbdl mlir dialect.
  // This could be its own function but why pollute the symbol table.
  C.DialectRegistry->insert<nbdl_spec::NbdlDialect>();

  // Assume that the MLIRContext cleans up ModuleOps.
  mlir::OpBuilder Builder(C.MLIRContext.get());
  mlir::Location Loc = Builder.getUnknownLoc();
  mlir::ModuleOp ModuleOp
    = mlir::ModuleOp::create(Builder, Loc, Name);
  C.Cont(ModuleOp.getOperation());
}

// Take an arbitrary set of string-like arguments that represent
// C++ typenames to create a !nbdl.store<typenames...>.
void nbdl_spec_create_store_type(schir::Context& C, schir::ValueRefs Args) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  llvm::SmallVector<mlir::StringAttr, 8> TypeNames;
  for (schir::Value Arg : Args) {
    llvm::StringRef Str = Arg.getStringRef();
    if (Str.empty())
      return C.RaiseError("expecting a nonempty string-like");
    auto StringAttr = mlir::StringAttr::get(Ctx, Str);
    TypeNames.push_back(StringAttr);
  }

  mlir::Type StoreT = nbdl_spec::StoreType::get(Ctx, TypeNames);
  schir::Value Result = C.CreateAny<mlir::Type>(StoreT);
  C.Cont(Result);
}

// Get the !nbdl.store typenames as a list of symbols
// or #f if the list is empty or mlir.value is not a !nbdl.store.
// We also accept '() since it is often used as a placeholder for the
// unit type.
void nbdl_spec_get_store_alts(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  schir::Value Arg = Args.front();

  if (isa<schir::Empty>(Arg))
    return C.Cont(schir::Bool(false));

  mlir::Value V = schir::any_cast<mlir::Value>(Arg);
  if (!V)
    return C.RaiseError("expecting mlir.value or '()", Arg);

  nbdl_spec::StoreType ST = dyn_cast<nbdl_spec::StoreType>(V.getType());
  if (!ST)
    return C.Cont(schir::Bool(false));

  llvm::SmallVector<schir::Value, 8> Results;
  for (mlir::StringAttr SA : ST.getAlts())
    Results.push_back(C.CreateSymbol(llvm::StringRef(SA)));

  if (Results.empty())
    return C.Cont(schir::Bool(false));

  C.Cont(C.CreateList(Results));
}

// Get the name of a mlir.value of type !nbdl.member_name by
// visiting its defining operation (which we expect should exist).
void nbdl_get_member_name(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  mlir::Value V = schir::any_cast<mlir::Value>(Args.front());
  if (!V || !isa<nbdl_spec::MemberNameType>(V.getType()))
    return C.RaiseError("expecting mlir.value of type !nbdl.member_name: {}",
                        Args.front());

  auto Op = V.getDefiningOp<nbdl_spec::MemberNameOp>();
  if (!Op)
    return C.RaiseError("mlir.value of type !nbdl.member_name"
                        "should be defined by nbdl_spec::MemberNameOp");

  llvm::StringRef Name = Op.getName();
  return C.Cont(C.CreateSymbol(Name));
}
} //  extern "C"
