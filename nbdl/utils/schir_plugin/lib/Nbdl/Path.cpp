// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Implement the !nbdl.path type and its static comparisons.
//
#include <nbdl_spec/NbdlDialect.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>

using namespace nbdl_spec;

// Verify the keys of a path.
llvm::LogicalResult
PathType::verify(llvm::function_ref<mlir::InFlightDiagnostic()> EmitError,
                 llvm::ArrayRef<mlir::TypeAttr> Keys) {
  for (mlir::TypeAttr Key : Keys) {
    if (!Key || !isa<MemberNameType, TagType, PathType>(Key.getValue()))
      return EmitError() << "expecting a !nbdl.member_name, a !nbdl.tag, "
                            "or a nested !nbdl.path as a path key: " << Key;
  }
  return mlir::success();
}

// Return true if the keys may be equal at runtime.
static bool mayEqualPathKeys(mlir::TypeAttr A, mlir::TypeAttr B) {
  return A == B || (isa<PathType>(A.getValue()) &&
                    isa<PathType>(B.getValue()));
}

bool nbdl_spec::isPathPrefix(PathType Prefix, PathType P) {
  if (Prefix.getKeys().size() > P.getKeys().size())
    return false;
  for (auto [A, B] : llvm::zip(Prefix.getKeys(), P.getKeys()))
    if (!mayEqualPathKeys(A, B))
      return false;
  return true;
}

bool nbdl_spec::mayAliasPaths(PathType A, PathType B) {
  return isPathPrefix(A, B) || isPathPrefix(B, A);
}
