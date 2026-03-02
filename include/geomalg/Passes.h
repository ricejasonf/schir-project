#ifndef GEOMALG_PASSES_H
#define GEOMALG_PASSES_H

#include <geomalg/Metric.h>
#include <geomalg/Dialect.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <llvm/Support/CommandLine.h>  // For ValuesClass

namespace geomalg {
// Generated stuff
#define GEN_PASS_DECL
#include "geomalg/GeomalgPasses.h.inc"

#define GEN_PASS_REGISTRATION
#include "geomalg/GeomalgPasses.h.inc"

inline llvm::cl::ValuesClass getMetricKindEnumValues() {
  return llvm::cl::values(
      clEnumValN(0, "unknown", "Unknown metric"),
      clEnumValN(1, "cga", "Conformal model"));
}

}  // namespace geomalg

#endif  // GEOMALG_PASSES_H
