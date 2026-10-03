// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Create C adapters for lowered functions so they may be called from C++
// without depending on how their lowered prototypes map to the C ABI.
//
#include <nbdl_spec/NbdlDialect.h>
#include <mlir/Conversion/LLVMCommon/MemRefBuilder.h>
#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/SymbolTable.h>
#include <mlir/Pass/Pass.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <string>

// Generated stuff
namespace nbdl_spec {
#define GEN_PASS_DEF_CADAPTERPASS
#include "nbdl_spec/NbdlPasses.h.inc"
}

namespace LLVM = mlir::LLVM;
using llvm::dyn_cast;
using llvm::isa;

namespace {
// Create a portable adapter for each function marked with the nbdl.c_adapter
// attribute.
//  llvm.func @nbdl_c_3foo4name(%result: !llvm.ptr, %arg_0: !llvm.ptr, ...) {
//    %0 = llvm.load %arg_0 : !llvm.ptr -> T0
//    ...
//    %1 = llvm.call @"::foo::name"(%0, ...)
//    llvm.store %1, %result
//    llvm.return
//  }
class CAdapterPass
    : public nbdl_spec::impl::CAdapterPassBase<CAdapterPass> {
  using Base = nbdl_spec::impl::CAdapterPassBase<CAdapterPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::ModuleOp M = getOperation();
    llvm::SmallVector<LLVM::LLVMFuncOp, 8> Fns;
    for (LLVM::LLVMFuncOp Fn : M.getOps<LLVM::LLVMFuncOp>())
      if (Fn->hasAttr(nbdl_spec::CAdapterAttrName))
        Fns.push_back(Fn);

    mlir::LLVMTypeConverter TC(&getContext());
    for (LLVM::LLVMFuncOp Fn : Fns)
      if (llvm::failed(createAdapter(M, Fn, TC)))
        return signalPassFailure();
  }

  llvm::LogicalResult createAdapter(mlir::ModuleOp M, LLVM::LLVMFuncOp Fn,
                                    mlir::LLVMTypeConverter const& TC) {
    auto Attr = dyn_cast<mlir::TypeAttr>(
        Fn->getAttr(nbdl_spec::CAdapterAttrName));
    auto FT = Attr ? dyn_cast<mlir::FunctionType>(Attr.getValue())
                   : mlir::FunctionType();
    if (!FT)
      return Fn.emitError("expecting a function type for ")
        << nbdl_spec::CAdapterAttrName;
    Fn->removeAttr(nbdl_spec::CAdapterAttrName);

    std::string Name = nbdl_spec::getCAdapterName(Fn.getSymName());
    if (mlir::SymbolTable::lookupSymbolIn(M, Name))
      return Fn.emitError("C adapter name is already defined: ") << Name;

    mlir::MLIRContext* Ctx = &getContext();
    mlir::Location Loc = Fn.getLoc();
    LLVM::LLVMFunctionType LoweredT = Fn.getFunctionType();
    unsigned NumResults = FT.getNumResults();
    llvm::SmallVector<mlir::Type, 8> PtrTs(NumResults + FT.getNumInputs(),
                                           LLVM::LLVMPointerType::get(Ctx));
    auto AdapterT = LLVM::LLVMFunctionType::get(
        LLVM::LLVMVoidType::get(Ctx), PtrTs);

    mlir::OpBuilder B(Fn);
    B.setInsertionPointAfter(Fn);
    auto Adapter = LLVM::LLVMFuncOp::create(B, Loc, Name, AdapterT);
    mlir::Block* Entry = Adapter.addEntryBlock(B);
    B.setInsertionPointToStart(Entry);

    // The pointees might be less aligned than
    // the ABI alignment of the lowered types.
    constexpr unsigned Alignment = 1;

    // Load the arguments where a memref descriptor is
    // expanded into its elements (as FuncToLLVM does.)
    llvm::SmallVector<mlir::Value, 8> Args;
    for (auto [I, T] : llvm::enumerate(FT.getInputs())) {
      mlir::Value Ptr = Entry->getArgument(NumResults + I);
      if (isa<mlir::UnrankedMemRefType>(T))
        return Fn.emitError("unranked memref is not supported by C adapter");
      if (auto MT = dyn_cast<mlir::MemRefType>(T)) {
        mlir::Type DescT = TC.convertType(MT);
        if (!DescT)
          return Fn.emitError("memref is not supported by C adapter: ") << MT;
        mlir::Value Desc = LLVM::LoadOp::create(B, Loc, DescT, Ptr,
                                                Alignment);
        mlir::MemRefDescriptor::unpack(B, Loc, Desc, MT, Args);
        continue;
      }
      if (Args.size() >= LoweredT.getNumParams())
        break;
      mlir::Type ParamT = LoweredT.getParamType(Args.size());
      Args.push_back(LLVM::LoadOp::create(B, Loc, ParamT, Ptr, Alignment));
    }
    if (!llvm::equal(mlir::ValueRange(Args).getTypes(),
                     LoweredT.getParams()))
      return Fn.emitError("lowered function type does not match ")
        << nbdl_spec::CAdapterAttrName << ": " << FT;

    auto Call = LLVM::CallOp::create(B, Loc, Fn, Args);

    // Store the results where multiple results are packed in a struct.
    if (NumResults == 1) {
      LLVM::StoreOp::create(B, Loc, Call.getResult(), Entry->getArgument(0),
                            Alignment);
    } else {
      for (unsigned I = 0; I < NumResults; ++I) {
        mlir::Value Result = LLVM::ExtractValueOp::create(
            B, Loc, Call.getResult(), I);
        LLVM::StoreOp::create(B, Loc, Result, Entry->getArgument(I),
                              Alignment);
      }
    }
    LLVM::ReturnOp::create(B, Loc, mlir::ValueRange());
    return llvm::success();
  }
};
}  // namespace
