#ifndef GEOMALG_TYPE_H
#undef GEOMALG_TYPE_H

#include <geomalg/Dialect.h>

// Declare utility functions for geomalg types.

namespace geomalg {
inline
bool isZero(mlir::Value V) {
  return isa<geomalg::ZeroType>(V.getType());
}

inline
bool isZero(mlir::Type T) {
  return isa<geomalg::ZeroType>(T);
}

inline
bool isUnknown(mlir::Value V) {
  return isa<geomalg::UnknownType>(V.getType());
}

inline
bool isUnknown(mlir::Type T) {
  return isa<geomalg::UnknownType>(T);
}

// Ensure blade types are nonnegative.
inline
mlir::Type getCanonicalType(mlir::Type Type) {
  if (auto BT = dyn_cast<geomalg::BladeType>(Type))
    return BT.getCanonicalType();
  return Type;
}

inline
bool isLikeBlades(mlir::Type A, mlir::Type B) {
  if (auto BTA = dyn_cast<geomalg::BladeType>(A))
    if (auto BTB = dyn_cast<geomalg::BladeType>(B))
      return BTA.getCanonicalTag() == BTB.getCanonicalTag();
  return false;
}

mlir::Type getTypeFromSum(mlir::Type A, mlir::Type B);

mlir::Type
createBladeType(llvm::ArrayRef<geomalg::BladeType> BladeTypes);

mlir::Type inferOuterProdResult(mlir::Value LHS, mlir::Value RHS);

// Create a canonicalized type for a multivector.
// The result may be a BladeType for a single term.
// Expect that BladeTypes is a nonempty list of BladeTypes.
mlir::Type
createMultivectorType(llvm::MutableArrayRef<geomalg::BladeType> BladeTypes);
}  // namespace geomalg

#endif
