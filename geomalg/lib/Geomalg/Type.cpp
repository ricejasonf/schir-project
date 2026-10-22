#include <geomalg/Dialect.h>
#include <geomalg/Type.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>

namespace geomalg {
// Implement utilities for construction and
// introspection of geomalg dialect types.

// If a pattern rewriter changes a result type
// it must be a known mapping A → B where the
// semantics of B is contained in A.
bool isValidNarrowing(mlir::Type A, mlir::Type B) {
  // Allowed narrowings are:
  //  Unknown -> Any
  //  Any -> Zero
  //  Multivector -> Blade (for Blade in Multivector Blades)
  //  Multivector -> Multivector (if resuling blades are a subset)
  // where Any is a type with known semantics.
  if (A == B)
    return true;

  // Unknown has no semantics (ie vacuous case).
  if (isUnknown(A))
    return true;

  // We must know the semantics.
  if (!isa<MultivectorLike, BladeType, VersorType, ZeroType>(A))
    return false;

  // A versor narrows by narrowing its factors.
  if (auto VA = dyn_cast<VersorType>(A)) {
    if (auto VB = dyn_cast<VersorType>(B))
      return VA.getFactors().size() == VB.getFactors().size() &&
             llvm::all_of(llvm::zip(VA.getFactors(), VB.getFactors()),
                          [](auto Pair) {
                            auto [FA, FB] = Pair;
                            return isValidNarrowing(FA, FB);
                          });
  }

  // If all other sum operands become zero then
  // a multivector becomes a single basis blade
  // provided it was a member of the sum to begin with.
  if (auto ML = dyn_cast<MultivectorLike>(A))
    if (auto BT = dyn_cast<BladeType>(B))
      return llvm::is_contained(ML.getBlades(), BT);

  if (auto MLA = dyn_cast<MultivectorLike>(A)) {
    if (auto MLB = dyn_cast<MultivectorLike>(B)) {
      if (MLB.isUnitary() || A.getTypeID() == B.getTypeID()) {
        llvm::SmallVector<BladeType, 8> MLA_BTs(MLA.getBlades());
        llvm::SmallVector<BladeType, 8> MLB_BTs(MLB.getBlades());
        llvm::sort(MLA_BTs);
        llvm::sort(MLB_BTs);
        return llvm::includes(MLA_BTs, MLB_BTs, compareBladeTypes);
      }
    }
  }

  if (isZero(B))
    return true;

  return false;
}

bool isLikeMultivector(mlir::Type A, mlir::Type B) {
  if (auto MA = dyn_cast<MultivectorLike>(A)) {
    if (auto MB = dyn_cast<MultivectorLike>(B)) {
      return llvm::equal(MA.getBlades(), MB.getBlades());
    }
  }
  return false;
}

// Create a BladeType from a wedge product of basis blades (nonempty).
mlir::Type createBladeType(llvm::ArrayRef<geomalg::BladeType> BladeTypes) {
  assert(!BladeTypes.empty());

  bool IsZero = false;

  llvm::SmallVector<geomalg::BladeTag> BladeTags;
  auto PushTag = [&](this auto& PushTag, geomalg::BladeTag BT) -> void {
    if (BT.isZero()) {
      IsZero = true;
    } else if (BT.getGrade() > 1) {
      auto [BT1, BT2] = BT.factor();
      PushTag(BT1);
      PushTag(BT2);
    } else if (BT.getGrade() == 1) {
      BladeTags.push_back(BT);
    }
    // Ignore nonzero scalars (ie grade == 0.)
  };

  for (geomalg::BladeType BT : BladeTypes)
    PushTag(BT.getBladeTag());

  BladeTag Tag = BladeTag::create(BladeTags);
  mlir::MLIRContext* MLIRContext = BladeTypes.front().getContext();
  return IsZero || Tag.isZero()
    ? mlir::Type(geomalg::ZeroType::get(MLIRContext))
    : mlir::Type(geomalg::BladeType::get(MLIRContext, Tag.getTag()));
}

// This may return a noncanonical blade type.
mlir::Type inferOuterProdResult(mlir::Value LHS, mlir::Value RHS) {
  mlir::MLIRContext* Ctx = LHS.getContext();
  mlir::Type TypeA = LHS.getType();
  mlir::Type TypeB = RHS.getType();
  auto A = dyn_cast<geomalg::BladeType>(TypeA);
  auto B = dyn_cast<geomalg::BladeType>(TypeB);
  if (A && B) {
    // This can return a "noncanonical" blade (ie not sorted order basis blades.)
    std::array<geomalg::BladeType, 2> BladeTypes{{A, B}};
    return geomalg::createBladeType(BladeTypes);
  } else if (geomalg::isZero(TypeA) || geomalg::isZero(TypeB)) {
    return geomalg::ZeroType::get(Ctx);
  } else {
    return geomalg::UnknownType::get(Ctx);
  }
}

template <typename T>
mlir::Type createMultivectorLikeType(
                  llvm::MutableArrayRef<BladeType> BladeTypes) {
  assert(!BladeTypes.empty());

  // Sort by tag.
  llvm::sort(BladeTypes, compareBladeTypes);

  // Unique by pointer-like equality. (mlir::Types are uniqued)
  auto EndItr = llvm::unique(BladeTypes);
  BladeTypes = BladeTypes.take_front(
      std::distance(BladeTypes.begin(), EndItr));

  // Return a BladeType if we can.
  if (BladeTypes.size() == 1)
    return BladeTypes.front();

  mlir::MLIRContext* MLIRContext = BladeTypes.front().getContext();
  return T::get(MLIRContext, BladeTypes);
}

template
mlir::Type createMultivectorLikeType<MultivectorType>(
                  llvm::MutableArrayRef<BladeType> BladeTypes);
template
mlir::Type createMultivectorLikeType<UnitVectorType>(
                  llvm::MutableArrayRef<BladeType> BladeTypes);
template
mlir::Type createMultivectorLikeType<UnitMultivectorType>(
                  llvm::MutableArrayRef<BladeType> BladeTypes);

mlir::Type createUnitType(llvm::MutableArrayRef<BladeType> BladeTypes) {
  if (llvm::all_of(BladeTypes, [](BladeType BT) { return BT.getGrade() == 1; }))
    return createMultivectorLikeType<UnitVectorType>(BladeTypes);
  return createMultivectorLikeType<UnitMultivectorType>(BladeTypes);
}

llvm::LogicalResult UnitMultivectorType::verify(
    llvm::function_ref<mlir::InFlightDiagnostic()> EmitError,
    llvm::ArrayRef<BladeType> Blades) {
  if (llvm::all_of(Blades, [](BladeType BT) { return BT.getGrade() == 1; }))
    return EmitError() << "expecting a blade of grade other than 1 "
                          "(use unit_vector)";
  return llvm::success();
}

llvm::LogicalResult VersorType::verify(
    llvm::function_ref<mlir::InFlightDiagnostic()> EmitError,
    llvm::ArrayRef<mlir::Type> Factors) {
  if (Factors.empty())
    return EmitError() << "expecting at least one versor factor";
  for (mlir::Type T : Factors)
    if (!isVector(T))
      return EmitError() << "expecting 1-vector versor factor: " << T;
  return llvm::success();
}

llvm::SmallVector<mlir::Type, 4> VersorType::getFactorTypes() const {
  return llvm::SmallVector<mlir::Type, 4>(getFactors());
}

bool MultivectorType::isVector() const {
  for (geomalg::BladeType BT : getBlades()) {
    if (BT.getGrade() != 1)
      return false;
  }
  return true;
}

// Check that all basis blade elements are orthonormal 1-blades
// (to allow the use of dot product).
bool isOrthogonalBasis(Metric const& M, MultivectorLike A, MultivectorLike B) {
  for (BladeType BT1 : A.getBlades())
    for (BladeType BT2 : B.getBlades()) {
      if (BT1.getGrade() != 1 || BT2.getGrade() != 1)
        return false;
      int Expected = BT1 == BT2 ? 1 : 0;
      if (M.dotProduct(BT1.getBladeTag(), BT2.getBladeTag()) != Expected)
        return false;
    }
  return true;
}

}  // namespace geomalg
