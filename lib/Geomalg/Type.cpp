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
  if (!isa<MultivectorLike, BladeType,
           UnknownType, ZeroType>(A))
    return false;

  // If all other sum operands become zero then
  // a multivector becomes a single basis blade
  // provided it was a member of the sum to begin with.
  if (auto ML = dyn_cast<MultivectorLike>(A))
    if (auto BT = dyn_cast<BladeType>(B))
      return llvm::is_contained(ML.getBlades(), BT);

  if (auto MLA = dyn_cast<MultivectorLike>(A)) {
    if (auto MLB = dyn_cast<MultivectorLike>(B)) {
      bool IsUV = isa<UnitVectorType>(B);
      // Blades are sorted during construction.
      if (IsUV || A.getTypeID() == B.getTypeID())
        return llvm::includes(MLA.getBlades(), MLB.getBlades());
    }
  }

  if (isZero(B))
    return true;

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

// Create a type for a multivector.
// The result may be a BladeType for a single term.
// Do not combine blades equivalent by antisymmetric property
// as we will expand these in passes that may prefer one ordering
// over another. (ie because (e2^e1)(e2^e1) == 1)
mlir::Type
createMultivectorType(llvm::MutableArrayRef<geomalg::BladeType> BladeTypes) {
  assert(!BladeTypes.empty());

  // Sort by tag.
  llvm::sort(BladeTypes, [](auto& A, auto& B) {
      return A.getTag() < B.getTag();
    });

  // Unique by pointer-like equality. (mlir::Types are uniqued)
  auto EndItr = llvm::unique(BladeTypes);
  BladeTypes = BladeTypes.take_front(
      std::distance(BladeTypes.begin(), EndItr));


  // Return a BladeType if we can.
  if (BladeTypes.size() == 1)
    return BladeTypes.front();

  mlir::MLIRContext* MLIRContext = BladeTypes.front().getContext();
  return geomalg::MultivectorType::get(MLIRContext, BladeTypes);
}

bool MultivectorType::isVector() const {
  for (geomalg::BladeType BT : getBlades()) {
    if (BT.getGrade() != 1)
      return false;
  }
  return true;
}

}  // namespace geomalg
