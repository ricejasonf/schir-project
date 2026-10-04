// Copyright 2026 Jason Rice

#include <nbdl_spec/NbdlDialect.h>
#include <nbdl_spec/TranslateCpp.h>
#include <schir/Context.h>
#include <schir/MappableToCpp.h>
#include <schir/Value.h>
#include <schir/MlirHelper.h>
#include <schir/SchirClang.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinDialect.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/LLVMIR/LLVMTypes.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
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

namespace {
// Map nbdl types to C++ types.
struct NbdlMappableToCpp : schir::MappableToCpp {
  using MappableToCpp::MappableToCpp;

  bool getCppTypename(mlir::Type T,
                      llvm::SmallVectorImpl<char>& Result) const override {
    llvm::raw_svector_ostream OS(Result);
    if (auto CT = dyn_cast<nbdl_spec::CppType>(T))
      OS << CT.getCppTypename();
    else if (isa<nbdl_spec::StringType>(T))
      OS << "::std::string_view";
    else
      return false;
    return true;
  }
};

// Map LLVM types to C++ types.
struct LLVMMappableToCpp : schir::MappableToCpp {
  using MappableToCpp::MappableToCpp;

  bool getCppTypename(mlir::Type T,
                      llvm::SmallVectorImpl<char>& Result) const override {
    if (!isa<mlir::LLVM::LLVMPointerType>(T))
      return false;
    llvm::raw_svector_ostream(Result) << "void*";
    return true;
  }
};

// Map builtin types to the C++ types in nbdl/spec/mlir.hpp.
struct BuiltinMappableToCpp : schir::MappableToCpp {
  using MappableToCpp::MappableToCpp;

  bool getCppTypename(mlir::Type T,
                      llvm::SmallVectorImpl<char>& Result) const override {
    llvm::raw_svector_ostream OS(Result);
    if (T.isSignlessInteger(32))
      OS << "::std::int32_t";
    else if (T.isF32())
      OS << "float";
    else if (auto VT = dyn_cast<mlir::VectorType>(T))
      return getVectorCppTypename(VT, Result);
    else if (auto MT = dyn_cast<mlir::MemRefType>(T))
      return getMemRefCppTypename(MT, Result);
    else
      return false;
    return true;
  }

  static bool getVectorCppTypename(mlir::VectorType VT,
                                   llvm::SmallVectorImpl<char>& Result) {
    if (VT.getRank() != 1 || VT.isScalable())
      return false;
    llvm::StringRef Name;
    mlir::Type ElT = VT.getElementType();
    if (ElT.isF32())
      Name = "vec_f32";
    else if (ElT.isSignlessInteger(32))
      Name = "vec_i32";
    else
      return false;
    llvm::raw_svector_ostream(Result)
      << "::nbdl::" << Name << '<' << VT.getDimSize(0) << '>';
    return true;
  }

  // Only map fully dynamic memrefs since nbdl::memref
  // has run-time sizes, strides, and offset.
  // e.g. memref<?x?xi32, strided<[?, ?], offset: ?>>
  static bool getMemRefCppTypename(mlir::MemRefType MT,
                                   llvm::SmallVectorImpl<char>& Result) {
    if (MT.getRank() == 0 || MT.getMemorySpace() ||
        !llvm::all_of(MT.getShape(), mlir::ShapedType::isDynamic))
      return false;
    auto Layout = dyn_cast<mlir::StridedLayoutAttr>(MT.getLayout());
    if (!Layout || !mlir::ShapedType::isDynamic(Layout.getOffset()) ||
        !llvm::all_of(Layout.getStrides(), mlir::ShapedType::isDynamic))
      return false;
    llvm::SmallString<64> ElT;
    if (!MappableToCpp::lookup(MT.getElementType(), ElT))
      return false;
    llvm::raw_svector_ostream(Result)
      << "::nbdl::memref<" << ElT << ", " << MT.getRank() << '>';
    return true;
  }
};

// Create a !nbdl.cpp type with the typename canonicalized by SchirClang.
std::optional<nbdl_spec::CppType> createCppType(schir::Context& C,
                                                schir::SchirClangImpl* Impl,
                                                llvm::StringRef Typename) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  schir::SchirClang SchirClang(Impl);
  std::string Canonical = SchirClang.ParseType(C.getLoc(), Typename);
  if (SchirClang.HasError()) {
    C.RaiseError(SchirClang.ErrorMsg);
    return std::nullopt;
  }
  return nbdl_spec::CppType::get(Ctx, Canonical);
}

// (translate-cpp op [port])
// Currently the "port" has to be a tagged llvm::raw_ostream
// or a schir::LexerWriterFnRef.
void translateCpp(Context& C, ValueRefs Args, nbdl_spec::TranslateMode Mode) {
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
      Result = nbdl_spec::translate_cpp(LWF, Op, Mode);
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
    Result = nbdl_spec::translate_cpp(LexerWriter, Op, Mode);
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
} // namespace

extern "C" {
// Translate a nbdl dialect operation to C++.
// (translate-cpp op [port])
void nbdl_spec_translate_cpp(Context& C, ValueRefs Args) {
  translateCpp(C, Args, nbdl_spec::TranslateMode::Definition);
}

// Declare a function (FuncOp) in C++ so it may be referenced by name.
// (declare-cpp op [port])
void nbdl_spec_declare_cpp(Context& C, ValueRefs Args) {
  translateCpp(C, Args, nbdl_spec::TranslateMode::Declaration);
}

// Define a C++ function that calls a function (FuncOp) lowered
// from MLIR via its C adapter.
// (define-lowered-wrapper op [port])
void nbdl_spec_define_lowered_wrapper(Context& C, ValueRefs Args) {
  translateCpp(C, Args, nbdl_spec::TranslateMode::LoweredWrapper);
}

// Mark a FuncOp to have a C adapter created by
// the nbdl-c-adapter pass once it is lowered.
// (mark-c-adapter op)
void nbdl_spec_mark_c_adapter(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  auto FuncOp = dyn_cast_or_null<mlir::func::FuncOp>(
      dyn_cast<mlir::Operation>(Args[0]));
  if (!FuncOp)
    return C.RaiseError("expecting func.func: {}", Args[0]);
  FuncOp->setAttr(nbdl_spec::CAdapterAttrName,
                  mlir::TypeAttr::get(FuncOp.getFunctionType()));
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
  // Functions exported via export-c may use arith and memref operations.
  C.DialectRegistry->insert<mlir::arith::ArithDialect,
                            mlir::memref::MemRefDialect>();
  C.DialectRegistry->addExtension(
    +[](mlir::MLIRContext*, nbdl_spec::NbdlDialect* D) {
      D->addInterfaces<NbdlMappableToCpp>();
    });
  C.DialectRegistry->addExtension(
    +[](mlir::MLIRContext*, mlir::BuiltinDialect* D) {
      D->addInterfaces<BuiltinMappableToCpp>();
    });
  C.DialectRegistry->addExtension(
    +[](mlir::MLIRContext*, mlir::LLVM::LLVMDialect* D) {
      D->addInterfaces<LLVMMappableToCpp>();
    });
  nbdl_spec::registerPasses();
  C.Cont();
}

// Map a mlir.type to an equivalent !nbdl.cpp type
// or #f if the type is not mappable to C++.
// (type->cpp-type type schir-clang)
void nbdl_spec_type_to_cpp_type(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  auto Type = schir::any_cast<mlir::Type>(Args[0]);
  if (!Type)
    return C.RaiseError("expecting mlir.type: {}", Args[0]);
  auto* Impl = schir::any_cast<schir::SchirClangImpl*>(Args[1]);
  if (!Impl)
    return C.RaiseError("expecting SchirClang object");

  llvm::SmallString<64> Typename;
  if (!schir::MappableToCpp::lookup(Type, Typename))
    return C.Cont(schir::Bool(false));

  std::optional<nbdl_spec::CppType> Result = createCppType(C, Impl, Typename);
  if (!Result)
    return;
  C.Cont(C.CreateAny<mlir::Type>(mlir::Type(*Result)));
}

// Create a !nbdl.cpp_alias type from a string-like C++ typename.
// Its typename is canonicalized to a !nbdl.cpp type by the flatten pass.
// (cpp-alias-type typename)
void nbdl_spec_cpp_alias_type(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  llvm::StringRef Typename = Args[0].getStringRef();
  if (Typename.empty())
    return C.RaiseError("expecting nonempty string-like: {}", Args[0]);
  mlir::Type Result = nbdl_spec::CppAliasType::get(C.MLIRContext.get(),
                                                   Typename);
  C.Cont(C.CreateAny<mlir::Type>(Result));
}

// Create a !nbdl.store<alts...> from an arbitrary set of mlir.types.
void nbdl_spec_create_store_type(schir::Context& C, schir::ValueRefs Args) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  llvm::SmallVector<mlir::TypeAttr, 8> TypeAttrs;
  bool HasPlaceholder = false;
  for (schir::Value Arg : Args) {
    auto Type = schir::any_cast<mlir::Type>(Arg);
    if (!Type)
      return C.RaiseError("expecting a mlir.type: {}", Arg);
    HasPlaceholder |= schir::isPlaceholder(Type);
    TypeAttrs.push_back(mlir::TypeAttr::get(Type));
  }
  // A store of a type that is not yet inferred is unresolved.
  if (HasPlaceholder)
    TypeAttrs.clear();

  mlir::Type StoreT = nbdl_spec::StoreType::get(Ctx, TypeAttrs);
  schir::Value Result = C.CreateAny<mlir::Type>(StoreT);
  C.Cont(Result);
}

// Return true if the mlir.value is a !nbdl.store.
void nbdl_spec_is_store(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::Value V = schir::any_cast<mlir::Value>(Args.front());
  C.Cont(schir::Bool(V && isa<nbdl_spec::StoreType>(V.getType())));
}

// Return true if the mlir.operation is a nbdl.define_store.
void nbdl_spec_is_define_store(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  auto* Op = dyn_cast<mlir::Operation>(Args.front());
  if (!Op)
    return C.RaiseError("expecting mlir.operation: {}", Args.front());
  C.Cont(schir::Bool(isa<nbdl_spec::DefineStoreOp>(Op)));
}

// Replace each !nbdl.cpp_alias in an operation with the
// !nbdl.cpp type of its canonical typename.
// (canonicalize-cpp-types op schir-clang)
void nbdl_canonicalize_cpp_types(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    return C.RaiseError("expecting mlir.operation");
  auto* Impl = schir::any_cast<schir::SchirClangImpl*>(Args[1]);
  if (!Impl)
    return C.RaiseError("expecting SchirClang object");

  llvm::LogicalResult Result = mlir_helper::WithDiagnosticsHandler(
    C, C.getLoc(),
    [&] { return nbdl_spec::canonicalizeCppTypes(Op, Impl); },
    "nbdl canonicalize C++ types failed");
  if (llvm::failed(Result))
    return;
  C.Cont();
}

void nbdl_run_flatten_pass(schir::Context& C, schir::ValueRefs Args) {
  if (Args.empty() || Args.size() > 2)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args.front());
  Args = Args.drop_front();

  if (!Op)
    return C.RaiseError("expecting mlir.operation");

  schir::SchirClangImpl* Impl = nullptr;
  if (Args.size() == 1) {
    Impl = schir::any_cast<schir::SchirClangImpl*>(Args.front());
    if (!Impl)
      return C.RaiseError("expecting SchirClang object");
  }

  llvm::LogicalResult Result = mlir_helper::WithDiagnosticsHandler(
    C, C.getLoc(),
    [&] { return nbdl_spec::runFlattenPass(Op, Impl); },
    "nbdl flatten pass failed");
  if (llvm::failed(Result))
    return;
  C.Cont();
}

// Run the flatten pass and the passes of each pipeline string
// until a fixed point is reached.
// (run-inference-passes op schir-clang pipeline ...)
void nbdl_run_inference_passes(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() < 2)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op)
    return C.RaiseError("expecting mlir.operation");
  auto* Impl = schir::any_cast<schir::SchirClangImpl*>(Args[1]);
  if (!Impl)
    return C.RaiseError("expecting SchirClang object");

  llvm::SmallVector<std::string, 4> Pipelines;
  for (schir::Value Arg : Args.drop_front(2)) {
    if (!isa<schir::String>(Arg))
      return C.RaiseError("expecting pass pipeline string: {}", Arg);
    Pipelines.push_back(Arg.getStringRef().str());
  }

  llvm::LogicalResult Result = mlir_helper::WithDiagnosticsHandler(
    C, C.getLoc(),
    [&] { return nbdl_spec::runInferencePasses(Op, Impl, Pipelines); },
    "nbdl inference passes failed");
  if (llvm::failed(Result))
    return;
  C.Cont();
}

} //  extern "C"
