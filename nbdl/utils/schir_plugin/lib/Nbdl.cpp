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

// Create a !nbdl.cpp type with the typename canonicalized
// by SchirClang or as is if Impl is nullptr.
std::optional<nbdl_spec::CppType> createCppType(schir::Context& C,
                                                schir::SchirClangImpl* Impl,
                                                llvm::StringRef Typename) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  if (!Impl)
    return nbdl_spec::CppType::get(Ctx, Typename);
  schir::SchirClang SchirClang(Impl);
  std::string Canonical = SchirClang.ParseType(C.getLoc(), Typename);
  if (SchirClang.HasError()) {
    C.RaiseError(SchirClang.ErrorMsg);
    return std::nullopt;
  }
  return nbdl_spec::CppType::get(Ctx, Canonical);
}
} // namespace

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
  C.DialectRegistry->addExtension(
    +[](mlir::MLIRContext*, nbdl_spec::NbdlDialect* D) {
      D->addInterfaces<NbdlMappableToCpp>();
    });
  C.DialectRegistry->addExtension(
    +[](mlir::MLIRContext*, mlir::BuiltinDialect* D) {
      D->addInterfaces<BuiltinMappableToCpp>();
    });
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

// Create a !nbdl.cpp type from a string-like C++ typename.
// Use an optional 'canonical tag to bypass canonicalizing
// the type via Clang.
// (cpp-type typename schir-clang ['canonical])
void nbdl_spec_cpp_type(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 2 && Args.size() != 3)
    return C.RaiseError("invalid arity");
  llvm::StringRef Typename = Args[0].getStringRef();
  if (Typename.empty())
    return C.RaiseError("expecting nonempty string-like: {}", Args[0]);
  auto* Impl = schir::any_cast<schir::SchirClangImpl*>(Args[1]);
  if (!Impl)
    return C.RaiseError("expecting SchirClang object");
  if (Args.size() == 3) {
    auto* Tag = dyn_cast<schir::Symbol>(Args[2]);
    if (!Tag || !Tag->Equiv("canonical"))
      return C.RaiseError("expecting tag 'canonical: {}", Args[2]);
    // The typename is already canonical.
    Impl = nullptr;
  }

  std::optional<nbdl_spec::CppType> Result = createCppType(C, Impl, Typename);
  if (!Result)
    return;
  C.Cont(C.CreateAny<mlir::Type>(mlir::Type(*Result)));
}

// Create a !nbdl.store<alts...> from an arbitrary set of mlir.types.
void nbdl_spec_create_store_type(schir::Context& C, schir::ValueRefs Args) {
  mlir::MLIRContext* Ctx = C.MLIRContext.get();
  llvm::SmallVector<mlir::TypeAttr, 8> TypeAttrs;
  for (schir::Value Arg : Args) {
    auto Type = schir::any_cast<mlir::Type>(Arg);
    if (!Type)
      return C.RaiseError("expecting a mlir.type: {}", Arg);
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

} //  extern "C"
