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

llvm::LogicalResult
geomalg::SumOp::inferReturnTypes(mlir::MLIRContext* Ctx,
                                 std::optional<mlir::Location> LocOpt,
                                 mlir::ValueRange Operands,
                                 mlir::DictionaryAttr Attrs,
                                 mlir::OpaqueProperties Props,
                                 mlir::RegionRange Regions,
                  llvm::SmallVectorImpl<mlir::Type>& InferredTypes) {
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  mlir::Type ResultType;
  for (mlir::Value V : Operands) {
    mlir::Type Type = V.getType();
    if (isUnknown(V)) {
      ResultType = geomalg::UnknownType::get(Ctx);
      break;
    }
    if (auto BT = dyn_cast<geomalg::BladeType>(Type))
      BladeTypes.push_back(BT);
    else if (auto MT = dyn_cast<geomalg::MultivectorType>(Type))
      llvm::append_range(BladeTypes, MT.getBlades());
    else {
      if (!geomalg::isZero(Type)) {
        mlir::Location Loc = LocOpt ? *LocOpt : mlir::UnknownLoc::get(Ctx);
        ResultType = geomalg::UnknownType::get(Ctx);
        mlir::emitError(Loc,
            "expecting a valid operand type to geomalg.sum");
        return llvm::failure();
      }
    }
  }
  if (!ResultType && BladeTypes.empty())
    ResultType = geomalg::ZeroType::get(Ctx);
  else if (!ResultType)
    ResultType = geomalg::createMultivectorType(BladeTypes);
  assert(ResultType && "expecting a valid type");
  InferredTypes.push_back(ResultType);
  return llvm::success();
}
