#ifndef GEOMALG_TYPE_H
#define GEOMALG_TYPE_H

#include <geomalg/Dialect.h>
#include <geomalg/Metric.h>

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

// Return true if the type is a placeholder for a type that
// is not yet inferred (e.g. !geomalg.unknown.)
inline
bool isUnknown(mlir::Type T) {
  return schir::isPlaceholder(T);
}

inline
bool isUnknown(mlir::Value V) {
  return isUnknown(V.getType());
}

inline
bool isUnitVector(mlir::Value V) {
  return isa<geomalg::UnitVectorType>(V.getType());
}

bool isValidNarrowing(mlir::Type A, mlir::Type B);

// Return true if the type is a blade of grade 1 or a sum of them.
inline
bool isVector(mlir::Type T) {
  if (auto BT = dyn_cast<geomalg::BladeType>(T))
    return BT.getGrade() == 1;
  if (auto MV = dyn_cast<geomalg::MultivectorLike>(T))
    return MV.isVector();
  return false;
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

bool isLikeMultivector(mlir::Type A, mlir::Type B);

inline
auto compareBladeTypes = [](BladeType const& A, BladeType const& B) {
  return A.getBladeTag() < B.getBladeTag();
};

mlir::Type getTypeFromSum(mlir::Type A, mlir::Type B);

mlir::Type
createBladeType(llvm::ArrayRef<geomalg::BladeType> BladeTypes);

mlir::Type inferOuterProdResult(mlir::Value LHS, mlir::Value RHS);

// Create a canonicalized type for a multivector.
// The result may be a BladeType for a single term.
// Expect that BladeTypes is a nonempty list of BladeTypes.

template <typename T>
mlir::Type
createMultivectorLikeType(llvm::MutableArrayRef<BladeType> BladeTypes);

bool isOrthogonalBasis(Metric const& M, MultivectorLike A, MultivectorLike B);

// Basis vector tags of the conformal origin and infinity.
inline constexpr uint32_t NoTag = 8;
inline constexpr uint32_t NiTag = 16;

// Get the blades of a blade or multivector type or none otherwise.
inline
llvm::SmallVector<BladeType, 8> getBladeTypes(mlir::Type T) {
  if (auto BT = dyn_cast<BladeType>(T))
    return {BT};
  if (auto MV = dyn_cast<MultivectorLike>(T))
    return llvm::SmallVector<BladeType, 8>(MV.getBlades());
  return {};
}

// Return true if the inverse of a value of type T has a supported
// closed form. (ie A⁻¹ = Ã / (A ⌋ Ã) for a blade or 1-vector and
// Ṽ / ⟨V Ṽ⟩₀ for a versor or an even multivector assumed to be a versor.)
inline
bool hasSupportedInverse(mlir::Type T) {
  if (isUnknown(T) || isZero(T) || isVector(T) ||
      isa<BladeType, VersorType>(T))
    return true;
  return llvm::none_of(getBladeTypes(T), [](BladeType BT) {
    return BT.getGrade() % 2 != 0;
  });
}

// Return true if a value of type From has a supported conversion to To.
// (ie widening to a type that From is a narrowing of, or normalizing
// a 1-vector to a unit_vector.)
inline
bool hasSupportedConversion(mlir::Type From, mlir::Type To) {
  if (isUnknown(From) || isUnknown(To) || isValidNarrowing(To, From))
    return true;
  auto MV = dyn_cast<MultivectorLike>(From);
  return MV && MV.isVector() && isa<UnitVectorType>(To);
}

}  // namespace geomalg

#endif
