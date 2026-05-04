#ifndef GEOMALG_DIALECT_H
#define GEOMALG_DIALECT_H

#include <geomalg/BladeTag.h>
#include <llvm/ADT/Hashing.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Dialect.h>
#include <mlir/IR/OpDefinition.h>
#include <mlir/Interfaces/ControlFlowInterfaces.h>
#include <mlir/Interfaces/SideEffectInterfaces.h>
#include <concepts>

namespace geomalg {
struct DistributiveTag { };
struct MultilinearTag { };
struct ZeroAbsorbTag { };
struct IsMulTag { };

template <typename T>
using Distributive = DistributiveTag;
template <typename T>
using Multilinear = MultilinearTag;
template <typename T>
using ZeroAbsorb = ZeroAbsorbTag;
template <typename T>
using IsMul = IsMulTag;

struct InferredResultBase {
  static bool isCompatibleReturnTypes(mlir::TypeRange Ls, mlir::TypeRange Rs);
};

template <typename T>
using InferredResult = InferredResultBase;

class BladeType;
} // namespace geomalg

// #pragma clang diagnostic push
// #pragma clang diagnostic ignored "-Wunused-parameter"

#include "geomalg/GeomalgDialect.h.inc"

#include "geomalg/GeomalgTypeInterfaces.h.inc"

#define GET_TYPEDEF_CLASSES
#include "geomalg/GeomalgTypes.h.inc"

#if 0 // We do no actually have custom attributes.
#define GET_ATTRDEF_CLASSES
#include "geomalg/GeomalgAttrs.h.inc"
#endif

#define GET_OP_CLASSES
#include "geomalg/GeomalgOps.h.inc"

// #pragma clang diagnostic pop

#endif  // GEOMALG_DIALECT_H
