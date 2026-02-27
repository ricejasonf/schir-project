#ifndef GEOMALG_BLADE_TAG_H
#define GEOMALG_BLADE_TAG_H

#include <llvm/ADT/ArrayRef.h>
#include <bit>
#include <utility>

namespace geomalg {
// Use a bitmask to represent a blade uniquely.
// Each bit represents a dimension expcept the high bit
// which represents sign. No bits set represents a scalar.
class BladeTag {
  uint32_t Tag = 0;

  static constexpr unsigned tag_bit_width = 32;
  static constexpr unsigned tag_sign_mask = 1 << (tag_bit_width - 1);

public:
  BladeTag() = default;
  explicit BladeTag(uint32_t Tag) : Tag(Tag) { }

  uint32_t getTag() const {
    return Tag;
  }

  uint32_t getCanonicalTag() const {  // Strip the sign bit.
    return ~tag_sign_mask & getTag();
  }

  unsigned getGrade() const {
    // Count all the bits except the sign bit.
    return std::popcount(getCanonicalTag());
  }

  // Basis vectors include 0-blades and 1-blades
  bool isBasisVector() const {
    // Include scalar as a basis vector.
    // Exclude negative blades.
    return getGrade() < 2 && isNonnegative();
  }


  bool isNonnegative() const {
    return !static_cast<bool>(tag_sign_mask & getTag());
  }

  BladeTag negate() const {
    return BladeTag(tag_sign_mask ^ getTag());
  }

  // Grade involution. B^ = (-1)^{grade(B)} B
  BladeTag invo() const {
    return getGrade() % 2 == 0 ? negate() :*this;
  }

  // Peel off the leftmost basis vector from the wedge product.
  // Return the 1-blade and the (k - 1)-blade factors.
  std::pair<BladeTag, BladeTag> factor() const {
    uint32_t CTag = getCanonicalTag();
    if (CTag == 0)
      return {BladeTag(0), *this};

    // We assume blades are in sorted order so we must take
    // the vector specified by the least significant bit.

    // TagB has all bits in Tag except the one set in TagA.
    // This will include the original sign bit.
    uint32_t TagA = 1 << std::countr_zero(CTag);
    uint32_t TagB = Tag ^ TagA;
    return {BladeTag(TagA), BladeTag(TagB)};
  }

  auto operator<=>(BladeTag const&) const = default;
  bool operator==(BladeTag const&) const = default;

  static BladeTag create(llvm::MutableArrayRef<BladeTag> BladeTags) {
    if (BladeTags.empty())
      return BladeTag();

    // Manually sort by canonical tag (ie without regard to sign bit.)
    // For each swap, we change the sign which may be incorrect if
    // elements are not unique, but we check that after sorting.
    uint32_t SignTag = 0;
    auto Swap = [&SignTag](geomalg::BladeTag& A, geomalg::BladeTag& B) {
        // We are expecting canonical basis vectors. (ie nonnegative)
        assert(A.isBasisVector() && B.isBasisVector());
        std::swap(A, B);
        SignTag ^= geomalg::BladeTag::tag_sign_mask;
      };
    auto LessThanEqual = [](auto& A, auto& B) {
        return A.getTag() <= B.getTag();
      };

    for (unsigned I = 0; I < BladeTags.size(); I++) {
      for (unsigned J = I + 1; J < BladeTags.size(); J++) {
        if (!LessThanEqual(BladeTags[I], BladeTags[J]))
          Swap(BladeTags[I], BladeTags[J]);
      }
    }

    // If we have more than one of any basis
    // element then the whole thing becomes zero
    // (which we would should be checking in a Pass.)
    size_t OrigSize = BladeTags.size();
    llvm::unique(BladeTags);
    if (OrigSize != BladeTags.size())
      return BladeTag();

    uint32_t Tag = 0;
    for (geomalg::BladeTag BladeTag : BladeTags)
      Tag |= BladeTag.getTag();
    
    // Incorporate the sign bit.
    Tag |= SignTag;

    return BladeTag(Tag);
  }
};
}  // namespace geomalg

#endif  // GEOMALG_BLADE_TAG_H
