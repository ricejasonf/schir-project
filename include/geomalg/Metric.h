#ifndef GEOMALG_METRIC_H
#undef GEOMALG_METRIC_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <bit>

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
  BladeTag(uint32_t Tag) : Tag(Tag) { }

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

// We are deliberately not enforcing the grades or signed of entries
// in the metric so that a full metric tensor could be built if desired.
class Metric {
  uint32_t Dim = 0;
  llvm::SmallVector<std::tuple<BladeTag, BladeTag, int>, 8> DotProducts;

  // TODO Validate BladeTags so that Grade <= Dim.
public:
  using Entry = decltype(DotProducts)::value_type;

  Metric(uint32_t Dim, std::initializer_list<Entry> Entries)
    : Dim(Dim),
      DotProducts(Entries)
  { }

  // Assume vectors are orthonormal unless specified otherwise.
  int DotProduct(BladeTag A, BladeTag B) {
    if (A > B)
      std::swap(A, B);

    auto Itr = llvm::find_if(DotProducts, [&](auto& Tuple) {
        auto [C, D, _] = Tuple;
        return (A == C && B == D);
      });

    if (Itr == DotProducts.end())
      return (A == B) ? 1 : 0;

    return std::get<2>(*Itr);
  }
};
}  // namespace geomalg


#endif  // GEOMALG_METRIC_H
