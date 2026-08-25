// Copyright 2026 Jason Rice

#include <nbdl_spec/NbdlDialect.h>
#include <nbdl_spec/TranslateCpp.h>
#include <schir/Context.h>
#include <schir/Value.h>
#include <schir/MlirHelper.h>
#include <schir/SchirClang.h>
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

extern "C" {
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

// Register the Nbdl MLIR dialect.
void nbdl_spec_register_nbdl_dialect(schir::Context& C,
                                     schir::ValueRefs Args) {
  if (Args.size() != 0)
    return C.RaiseError("invalid arity");
  C.DialectRegistry->insert<nbdl_spec::NbdlDialect>();
  C.Cont();
}

// Take an arbitrary set of string-like arguments that represent
// C++ typenames to create a !nbdl.store<typenames...>.
void nbdl_spec_create_store_type(schir::Context& C, schir::ValueRefs Args) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  llvm::SmallVector<mlir::TypeAttr, 8> TypeAttrs;
  for (schir::Value Arg : Args) {
    llvm::StringRef Str = Arg.getStringRef();
    mlir::Type Type;
    if (!Str.empty()) {
      auto StringAttr = mlir::StringAttr::get(Ctx, Str);
      Type = nbdl_spec::CppType::get(Ctx, Str);
    } else if (auto T = schir::any_cast<mlir::Type>(Arg)) {
      Type = T;
    } else {
      C.RaiseError("expecting a mlir.type or string-like: {}", Arg);
    }
    TypeAttrs.push_back(mlir::TypeAttr::get(Type));
  }

  mlir::Type StoreT = nbdl_spec::StoreType::get(Ctx, TypeAttrs);
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
  for (mlir::TypeAttr SA : ST.getAlts()) {
    mlir::Type Type = SA.getValue();
    if (auto CppType = dyn_cast<nbdl_spec::CppType>(Type)) {
      // Map CppType back to Symbol.
      // TODO Maybe we do not do this.
      llvm::StringRef Name = CppType.getCppTypename();
      Results.push_back(C.CreateSymbol(Name));
    } else {
      Results.push_back(C.CreateAny<mlir::Type>(Type));
    }
  }

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

void nbdl_run_flatten_pass(schir::Context& C, schir::ValueRefs Args) {
  if (Args.empty() || Args.size() > 2)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args.front());
  mlir::ModuleOp ModuleOp = dyn_cast_or_null<mlir::ModuleOp>(Op);
  Args = Args.drop_front();

  if (!ModuleOp)
    return C.RaiseError("expecting ModuleOp");

  schir::SchirClangImpl* Impl = nullptr;
  if (Args.size() == 1) {
    Impl = schir::any_cast<schir::SchirClangImpl*>(Args.front());
    if (!Impl)
      return C.RaiseError("expecting SchirClang object");
  }

  llvm::LogicalResult Result = mlir_helper::WithDiagnosticsHandler(
    C, C.getLoc(),
    [&] { return nbdl_spec::runFlattenPass(ModuleOp, Impl); },
    "nbdl flatten pass failed");
  if (llvm::failed(Result))
    return;
  C.Cont();
}

} //  extern "C"
