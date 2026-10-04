// Copyright Jason Rice 2025
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Include all of the generated header/source files for the nbdl mlir dialect.
// 
#include <nbdl_spec/NbdlDialect.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/OpImplementation.h>
#include <llvm/ADT/TypeSwitch.h>

// Include generated source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include "nbdl_spec/NbdlDialect.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "nbdl_spec/NbdlTypes.cpp.inc"
#define GET_ATTRDEF_CLASSES
#include "nbdl_spec/NbdlAttrs.cpp.inc"
#define GET_OP_CLASSES
#include "nbdl_spec/NbdlOps.cpp.inc"

void nbdl_spec::NbdlDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "nbdl_spec/NbdlTypes.cpp.inc"
    >();
  addAttributes<
#define GET_ATTRDEF_LIST
#include "nbdl_spec/NbdlAttrs.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "nbdl_spec/NbdlOps.cpp.inc"
      >();

}
#pragma clang diagnostic pop

nbdl_spec::StoreType
nbdl_spec::StoreType::get(mlir::MLIRContext* Ctx,
                          llvm::ArrayRef<mlir::TypeAttr> TypeAttrsRef) {
  llvm::SmallVector<mlir::TypeAttr, 8> TypeAttrs(TypeAttrsRef);
  // Sort and unique the inputs. (not stable)
  llvm::sort(TypeAttrs, [](auto const& A, auto const& B) {
      void const* AT = A.getValue().getTypeID().getAsOpaquePointer();
      void const* BT = B.getValue().getTypeID().getAsOpaquePointer();
      return AT < BT;
    });
  TypeAttrsRef = llvm::ArrayRef(TypeAttrs.begin(), llvm::unique(TypeAttrs));
  return Base::get(Ctx, TypeAttrsRef);
}

void nbdl_spec::ScopeOp::build(::mlir::OpBuilder&,
                              ::mlir::OperationState& odsState,
                              std::unique_ptr<::mlir::Region>&& body) {
  odsState.addRegion(std::move(body));
}

// Get the FuncNameOp that defines V looking through
// a !nbdl.store<!nbdl.func_name> created by LiftStoreOp.
// Return a null op if V is not a function name.
static
nbdl_spec::FuncNameOp getFuncName(mlir::Value V) {
  if (auto Op = V.getDefiningOp<nbdl_spec::LiftStoreOp>())
    V = Op.getValue();
  return V.getDefiningOp<nbdl_spec::FuncNameOp>();
}

mlir::func::FuncOp nbdl_spec::VisitOp::lookupCallee() {
  nbdl_spec::FuncNameOp FN = getFuncName(getFn());
  auto M = (*this)->getParentOfType<mlir::ModuleOp>();
  if (!FN || !M)
    return {};
  return M.lookupSymbol<mlir::func::FuncOp>(FN.getName());
}

llvm::LogicalResult nbdl_spec::LiftStoreOp::verify() {
  mlir::Type ValueT = getValue().getType();
  if (llvm::isa<nbdl_spec::StoreType>(ValueT))
    return emitOpError("cannot lift a value that is already a store");
  // A placeholder value lifts to an unresolved store.
  auto ExpectedT = schir::isPlaceholder(ValueT)
    ? nbdl_spec::StoreType::get(getContext())
    : nbdl_spec::StoreType::get(getContext(), mlir::TypeAttr::get(ValueT));
  if (getResult().getType() != ExpectedT)
    return emitOpError("result type (") << getResult().getType()
      << ") should be " << ExpectedT;
  return llvm::success();
}

std::string nbdl_spec::getCAdapterName(llvm::StringRef SymName) {
  // TODO Consider using schir::Mangle. (It requires schir::Context.)
  std::string Result = "nbdl_c_";
  llvm::SmallVector<llvm::StringRef, 4> Parts;
  SymName.split(Parts, "::", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (llvm::StringRef Part : Parts)
    Result += std::to_string(Part.size()) + Part.str();
  return Result;
}
