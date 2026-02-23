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

// Create a BladeType from a wedge product of nonnegative basis vectors
// which may yield the ZeroType. The input array will be sorted in place.
mlir::Type
createBladeType(llvm::MutableArrayRef<geomalg::BladeType> BladeTypes) {
  assert(!BladeTypes.empty());
  llvm::SmallVector<geomalg::BladeTag> BladeTags(BladeTypes.size());
  for (unsigned I = 0; I < BladeTypes.size(); I++)
    BladeTags[I] = BladeTypes[I].getTag();
  BladeTag BT = BladeTag::create(BladeTags);

  mlir::MLIRContext* MLIRContext = BladeTypes.front().getContext();
  return geomalg::BladeType::get(MLIRContext, BT.getTag());
}

// Create a canonicalized type for a multivector.
// The result may be a BladeType for a single term.
mlir::Type
createMultivectorType(llvm::MutableArrayRef<geomalg::BladeType> BladeTypes) {
  if (BladeTypes.empty())
    return geomalg::ZeroType();

  // Transform all negative blades to positive.
  llvm::transform(BladeTypes, BladeTypes.begin(),
      [](auto BT) { return BT.getCanonicalType(); });

  // Sort by canonical tag and sign.
  llvm::sort(BladeTypes, [](auto& A, auto& B) {
      return (A.getCanonicalTag() < B.getCanonicalTag()) ||
             (A.isNonnegative() && !B.isNonnegative());
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

}  // namespace geomalg
