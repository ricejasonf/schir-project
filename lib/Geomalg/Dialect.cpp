#include <geomalg/Dialect.h>
#include <geomalg/Type.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/OpImplementation.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/TypeSwitch.h>

// Include generated source files (from the build dir.)
#include "geomalg/GeomalgDialect.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "geomalg/GeomalgTypes.cpp.inc"

#include "geomalg/GeomalgTypeInterfaces.cpp.inc"

#if 0 // We do not have custom attributes yet.
#define GET_ATTRDEF_CLASSES
#include "geomalg/GeomalgAttrs.cpp.inc"
#endif
#define GET_OP_CLASSES
#include "geomalg/GeomalgOps.cpp.inc"

void geomalg::GeomalgDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "geomalg/GeomalgTypes.cpp.inc"
    >();

#if 0 // We do not have custom attributes yet.
  addAttributes<
#define GET_ATTRDEF_LIST
#include "geomalg/GeomalgAttrs.cpp.inc"
      >();
#endif

  addOperations<
#define GET_OP_LIST
#include "geomalg/GeomalgOps.cpp.inc"
      >();

}

using namespace geomalg;


bool
InferredResultBase::isCompatibleReturnTypes(mlir::TypeRange Ls,
                                            mlir::TypeRange Rs) {
  // Inferred type must be narrower or equal to the actual type.
  // Ls - inferred result types
  // Rs - actual operation result types
  if (Ls.size() != Rs.size())
    return false;

  // Using the map R → L,
  for (auto [L, R] : llvm::zip(Ls, Rs))
    if (!isValidNarrowing(R, L))
      return false;
  return true;
}

llvm::LogicalResult
geomalg::ReverseOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  llvm::append_range(InferredTypes, Operands.getTypes());
  return llvm::success();
}
llvm::LogicalResult
geomalg::NegateOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  llvm::append_range(InferredTypes, Operands.getTypes());
  return llvm::success();
}
llvm::LogicalResult
geomalg::GradeInvoOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  llvm::append_range(InferredTypes, Operands.getTypes());
  return llvm::success();
}

llvm::LogicalResult
geomalg::SumOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr Attributes,
                  mlir::OpaqueProperties Properties,
                  mlir::RegionRange Regions,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  SumOpAdaptor Adaptor(Operands, Attributes, Properties, Regions);
  bool IsUnit = Adaptor.getIsUnit();
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  mlir::Type ResultT;
  for (mlir::Value V : Operands) {
    mlir::Type Type = V.getType();
    if (isa<UnknownType, MultivectorType>(V.getType())) {
      InferredTypes.push_back(geomalg::UnknownType::get(Ctx));
      return llvm::success();
    } else if (isa<ZeroType>(V.getType())) {
      // Ignore Zeros.
    } else if (auto BT = dyn_cast<geomalg::BladeType>(Type)) {
      BladeTypes.push_back(BT);
    } else {
      mlir::Location Loc = LocOpt ? *LocOpt : mlir::UnknownLoc::get(Ctx);
      mlir::emitError(Loc,
          "expecting a valid operand type to geomalg.sum");
      return llvm::failure();
    }
  }

  if (BladeTypes.empty())
    ResultT = geomalg::ZeroType::get(Ctx);
  else if (llvm::all_equal(BladeTypes))
    ResultT = BladeTypes.front();
  else if (IsUnit)
    ResultT = geomalg::UnitVectorType::get(Ctx, BladeTypes);
  else
    ResultT = geomalg::MultivectorType::get(Ctx, BladeTypes);

  InferredTypes.push_back(ResultT);
  return llvm::success();
}

llvm::LogicalResult
geomalg::ExpandOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  mlir::Type ArgT = Operands.front().getType();
  auto MV = dyn_cast<MultivectorLike>(ArgT);
  // Unary sums just propagate their argument.
  if (!MV) {
    InferredTypes.push_back(ArgT);
    return llvm::success();
  }

  for (BladeType BT : MV.getBlades())
    InferredTypes.push_back(BT);
  return llvm::success();
}

llvm::LogicalResult
geomalg::OuterProdOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  if (Operands.size() != 2)
    return llvm::failure();

  mlir::Type ResultT = geomalg::inferOuterProdResult(Operands[0], Operands[1]);
  InferredTypes.push_back(ResultT);
  return llvm::success();
}

llvm::LogicalResult
geomalg::OSwapOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  if (Operands.size() != 1)
    return llvm::failure();

  mlir::Type T = Operands.front().getType();
  auto BT = dyn_cast<geomalg::BladeType>(T);
  if (!BT)
    return llvm::failure();

  InferredTypes.push_back(BT.oswap());
  return llvm::success();
}

llvm::LogicalResult
geomalg::InverseOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  if (Operands.size() != 1)
    return llvm::failure();

  mlir::Type T = Operands.front().getType();
  auto BT = dyn_cast<geomalg::BladeType>(T);

  // Let operations that expand have unknown results.
  if (BT && BT.getGrade() <= 1)
    InferredTypes.push_back(T);
  else
    InferredTypes.push_back(geomalg::UnknownType::get(Ctx));

  return llvm::success();
}

llvm::LogicalResult
geomalg::InnerProdOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  mlir::Type TA = Operands[0].getType();
  mlir::Type TB = Operands[1].getType();
  auto A = dyn_cast<geomalg::BladeType>(TA);
  auto B = dyn_cast<geomalg::BladeType>(TB);
  mlir::Type Result;
  if (A && B) {
    if (A.getGrade() == 0)
      Result = B;
    else if (B.getGrade() == 0)
      Result = ZeroType::get(Ctx);
    else if (A.getGrade() == 1 && B.getGrade() == 1)
      Result = BladeType::get(Ctx, 0); // Scalar
  } else if (isZero(TA) || isZero(TB)) {
    Result = ZeroType::get(Ctx);
  }
  if (!Result)
    Result = geomalg::UnknownType::get(Ctx);

  InferredTypes.push_back(Result);
  return llvm::success();
}

llvm::LogicalResult
geomalg::MatmulOp::inferReturnTypes(
                  mlir::MLIRContext* Ctx,
                  std::optional<mlir::Location> LocOpt,
                  mlir::ValueRange Operands,
                  mlir::DictionaryAttr,
                  mlir::OpaqueProperties,
                  mlir::RegionRange Regions,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  // Iterate the regions collecting the all of the blades from
  // the return types. Unknown absorbs. Zero is identity.
  llvm::SmallVector<BladeType, 8> BladeTypes;
  for (mlir::Region* Region : Regions) {
    mlir::Type T;
    if (!Region->empty()) {
      auto ReturnOp = Region->front().getTerminator();
      T = ReturnOp->getOperand(0).getType();
    }

    if (!T || isUnknown(T)) {
      InferredTypes.push_back(UnknownType::get(Ctx));
      return llvm::success();
    }

    if (auto MV = dyn_cast<MultivectorLike>(T))
      llvm::append_range(BladeTypes, MV.getBlades());
    else if (auto BT = dyn_cast<BladeType>(T))
      BladeTypes.push_back(BT);
    else
      assert(isZero(T) && "expecting zero as the only remaining case");
  }

  mlir::Type Result;
  if (BladeTypes.empty())
    Result = ZeroType::get(Ctx);
  else
    Result = createMultivectorType(BladeTypes);

  InferredTypes.push_back(Result);
  return llvm::success();
}
