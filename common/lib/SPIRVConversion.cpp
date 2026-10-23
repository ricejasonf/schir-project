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
}

void populateFromElementsToArrayPatterns(SPIRVTypeConverter& TC,
                                         mlir::RewritePatternSet& PS) {
  PS.add<FromElementsToArray>(TC, PS.getContext(), /*benefit=*/2);
}
} // namespace schir_common
