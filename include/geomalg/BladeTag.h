#ifndef GEOMALG_BLADE_TAG_H
#define GEOMALG_BLADE_TAG_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <bit>
#include <utility>

namespace geomalg {
// Use a bitmask to represent a blade uniquely.
// Each bit represents a dimension expcept the high bit
// which represents sign with regard to canonical ordering
// of basis vectors which is sorted by tag value of basis
// vector tags.
// No bits set represents a scalar.
// Only the sign bit being set represents zero.
class BladeTag {
  uint32_t Tag = 0;

  static constexpr unsigned tag_bit_width = 32;
  static constexpr unsigned tag_sign_mask = 1 << (tag_bit_width - 1);

public:
  BladeTag() = default;
  explicit BladeTag(uint32_t Tag) : Tag(Tag) {
    // If the blade cannot have an noncanonical ordering
    assert((getGrade() != 1 || Tag == getCanonicalTag()) &&
        "1-blades cannot have noncanonical ordering");
  }

  uint32_t getTag() const {
    return Tag;
  }

  uint32_t getCanonicalTag() const {  // Strip the sign bit.
    return ~tag_sign_mask & getTag();
  }

  BladeTag getCanonical() const {
    return BladeTag(getCanonicalTag());
  }

  unsigned getGrade() const {
    // Count all the bits except the sign bit.
    return std::popcount(getCanonicalTag());
  }

  bool isCanonical() const {
    return !static_cast<bool>(tag_sign_mask & getTag());
  }

  // Denote noncanonical scalar as zero.
  bool isZero() {
    return Tag == tag_sign_mask;
  }

  // Change to or from the set of canonical orderings
  // of basis vectors.
  // Note that "sign" only indicates the canonical ordering
  // without regard to the sign of the coefficient.
  BladeTag oswap() const {
    return getGrade() > 1 ? BladeTag(tag_sign_mask ^ getTag())
                          : *this;
  }

  // Grade involution. B^ = (-1)^{grade(B)} B
  bool shouldInvoNegate() const {
    return getGrade() % 2 != 0;
  }

  bool shouldReverseNegate() const {
    unsigned G = getGrade();
    // Negate if reversing G basis elements performs an odd number of swaps
    // (because of the antisymmetric property of the wedge product.)
    return G % 2 == 0 || G % 3 == 0;
  }

  // Peel off the leftmost basis vector from the basis blade.
  // Return the 1-blade and the (k - 1)-blade factors.
  std::pair<BladeTag, BladeTag> factor() const {
    if (getGrade() < 2)
      return {BladeTag(0), *this};

    uint32_t CTag = getCanonicalTag();

    // We assume blades are in sorted order so we must take
    // the vector specified by the least significant bit.

    // TagB has all bits in Tag except the one set in TagA.
    // This will include the original sign bit.
    uint32_t TagA = 1 << std::countr_zero(CTag);
    uint32_t TagB = Tag ^ TagA;
    if (!isCanonical()) {
      // Get the second "leftmost" basis element.
      // Note that TagB will now be canonically ordered.
      assert(getGrade() > 1);
      TagA = 1 << std::countr_zero(TagB);
      TagB = CTag ^ TagA;
    }
    assert(BladeTag(TagA).getGrade() == 1);
    return {BladeTag(TagA), BladeTag(TagB)};
  }

  // Break a basis blade down into 1-vector wedge product factors.
  void factorize(llvm::SmallVectorImpl<BladeTag>& Result) {
    BladeTag A = *this;
    while (A.getGrade() > 1) {
      auto [a, B] = A.factor();
      Result.push_back(a);
      A = B;
    }
    Result.push_back(A);
  }

  std::strong_ordering operator<=>(BladeTag const& B) const {
    // Order by grade and then by the tag value.
    return Tag == B.Tag
              ? std::strong_ordering::equal :
           getGrade() < B.getGrade() || Tag < B.Tag
              ? std::strong_ordering::less
              : std::strong_ordering::greater;
  }

  bool operator==(BladeTag const&) const = default;

  static BladeTag createZero() {
    return BladeTag(geomalg::BladeTag::tag_sign_mask);
  }

  // This behaves like the wedge product of basis 1-blades.
  static BladeTag create(llvm::MutableArrayRef<BladeTag> BladeTags) {
    if (BladeTags.empty())
      return BladeTag();

    assert(llvm::all_of(BladeTags,
            [](auto B) { return B.getGrade() == 1; }));

    // First check for duplicate elements which results in zero.
    if (BladeTags.size() > 1) {
      uint32_t Sum = BladeTags.front().getCanonicalTag();
      for (BladeTag BT : BladeTags.drop_front())
        Sum |= BT.getCanonicalTag();
      if (std::popcount(Sum) < BladeTags.size())
        return BladeTag::createZero();
    }

    // Manually sort by canonical tag (ie without regard to sign bit.)
    // For each swap, we change the sign which may be incorrect if
    // elements are not unique, but we check that after sorting.
    uint32_t SignTag = 0;
    auto Swap = [&SignTag](geomalg::BladeTag& A, geomalg::BladeTag& B) {
        // We are expecting basis 1-blades.
        assert(A.getGrade() == 1 && B.getGrade() == 1);
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
