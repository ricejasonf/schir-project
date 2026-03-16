#ifndef GEOMALG_METRIC_H
#define GEOMALG_METRIC_H

#include <geomalg/BladeTag.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>

namespace geomalg {
// List of supported metrics.
enum class MetricKind {
  unknown = 0,
  cga = 1,
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

  operator bool() const { return Dim != 0; }

  // Assume vectors are orthonormal unless specified otherwise.
  // This is designed to return {-1, 0, 1}.
  int dotProduct(BladeTag A, BladeTag B) const {
    assert(A.getGrade() == 1 && B.getGrade() == 1);
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

  static Metric get(MetricKind Kind) {
    switch (Kind) {
    case MetricKind::unknown:
      return Metric(0, {});
    case MetricKind::cga:
      return Metric(5, {Entry{8, 8, 0},
                        Entry{16, 16, 0},
                        Entry{8, 16, -1}});
    }
  }
};
}  // namespace geomalg


#endif  // GEOMALG_METRIC_H
