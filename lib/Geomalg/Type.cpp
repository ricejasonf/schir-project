#include <geomalg/Dialect.h>
#include <geomalg/Type.h>
#include <heavy/Context.h>
#include <heavy/MlirHelper.h>
#include <heavy/Value.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

namespace geomalg {
// Implement utilities for construction and
// introspection of geomalg dialect types.

// Create a BladeType from a wedge product of basis 1-blades.
// The input array will be sorted in place.
mlir::Type
createBladeType(llvm::MutableArrayRef<geomalg::BladeType> BladeTypes) {
  assert(!BladeTypes.empty());
  llvm::SmallVector<geomalg::BladeTag> BladeTags(BladeTypes.size());
  for (unsigned I = 0; I < BladeTypes.size(); I++)
    BladeTags[I] = geomalg::BladeTag(BladeTypes[I].getTag());
  BladeTag BT = BladeTag::create(BladeTags);

  mlir::MLIRContext* MLIRContext = BladeTypes.front().getContext();
  return geomalg::BladeType::get(MLIRContext, BT.getTag());
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

// K is the grade of the blade.
bool MultivectorType::isBlade(unsigned K) const {
  uint32_t Sum = 0;
  for (auto BT : getBlades()) {
    Sum &= BT.getCanonicalTag().getTag();
    if (BT.getGrade() != K)
      return false;
  }

  // If this is not a vector there must be
  // a common basis vector or it cannot be
  // factored as an outer product of vectors.
  return K == 1 || Sum != 0;
}

}  // namespace geomalg
