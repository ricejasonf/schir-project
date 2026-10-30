// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
#include <schir_common/SPIRVConversion.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVOps.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVTypes.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/Transforms/DialectConversion.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <optional>

namespace spirv = mlir::spirv;
namespace vector = mlir::vector;

namespace {
// Construct arrays from elements.
struct FromElementsToArray
    : mlir::OpConversionPattern<vector::FromElementsOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        vector::FromElementsOp Op, vector::FromElementsOp::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ResultT = getTypeConverter()->convertType(Op.getType());
    auto ArrayT = llvm::dyn_cast_or_null<spirv::ArrayType>(ResultT);
    if (!ArrayT)
      return llvm::failure();
    R.replaceOpWithNewOp<spirv::CompositeConstructOp>(Op, ArrayT,
                                                      Adaptor.getElements());
    return llvm::success();
  }
};
} // namespace

namespace schir_common {
bool isSupportedSPIRVVector(spirv::TargetEnv const& TE, mlir::VectorType VT) {
  if (!spirv::CompositeType::isValid(VT))
    return false;
  llvm::SmallVector<llvm::ArrayRef<spirv::Capability>, 2> Capabilities;
  llvm::cast<spirv::CompositeType>(VT).getCapabilities(Capabilities);
  return llvm::all_of(Capabilities,
                      [&](llvm::ArrayRef<spirv::Capability> AnyOf) {
                        return TE.allows(AnyOf).has_value();
                      });
}

SPIRVTypeConverter::SPIRVTypeConverter(spirv::TargetEnvAttr TEA)
  : mlir::SPIRVTypeConverter(TEA)
{
  addConversion([this](mlir::VectorType VT) -> std::optional<mlir::Type> {
    if (VT.getRank() != 1 || VT.getNumElements() == 1 ||
        isSupportedSPIRVVector(getTargetEnv(), VT))
      return std::nullopt;
    mlir::Type ElementT = convertType(VT.getElementType());
    if (!ElementT)
      return std::nullopt;
    return spirv::ArrayType::get(ElementT, VT.getNumElements());
  });
  // Lay out a memref of 3-element vectors with the array stride of
  // 4-element vectors as the standard layout aligns them the same.
  addConversion([this](mlir::MemRefType MT) -> std::optional<mlir::Type> {
    auto VT = llvm::dyn_cast<mlir::VectorType>(MT.getElementType());
    if (!VT || VT.getShape() != llvm::ArrayRef<int64_t>{3} || VT.isScalable())
      return std::nullopt;
    auto PaddedT = mlir::MemRefType::get(
        MT.getShape(), mlir::VectorType::get({4}, VT.getElementType()),
        MT.getLayout(), MT.getMemorySpace());
    auto PtrT = llvm::dyn_cast_or_null<spirv::PointerType>(
        convertType(PaddedT));
    auto StructT = PtrT
      ? llvm::dyn_cast<spirv::StructType>(PtrT.getPointeeType())
      : spirv::StructType();
    mlir::Type ElementT = convertType(VT);
    if (!StructT || StructT.getNumElements() != 1 || !StructT.hasOffset() ||
        !ElementT)
      return std::nullopt;
    mlir::Type ArrayT;
    mlir::Type PaddedArrayT = StructT.getElementType(0);
    if (auto RT = llvm::dyn_cast<spirv::RuntimeArrayType>(PaddedArrayT))
      ArrayT = spirv::RuntimeArrayType::get(ElementT, RT.getArrayStride());
    else if (auto AT = llvm::dyn_cast<spirv::ArrayType>(PaddedArrayT))
      ArrayT = spirv::ArrayType::get(ElementT, AT.getNumElements(),
                                     AT.getArrayStride());
    else
      return std::nullopt;
    return spirv::PointerType::get(spirv::StructType::get({ArrayT}, {0}),
                                   PtrT.getStorageClass());
  });
}

void populateFromElementsToArrayPatterns(SPIRVTypeConverter& TC,
                                         mlir::RewritePatternSet& PS) {
  PS.add<FromElementsToArray>(TC, PS.getContext(), /*benefit=*/2);
}
} // namespace schir_common
