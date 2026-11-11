// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Implement the !nbdl.path type and its static comparisons.
//
#include <nbdl_spec/NbdlDialect.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>

using namespace nbdl_spec;

// Verify the keys of a path.
llvm::LogicalResult
PathType::verify(llvm::function_ref<mlir::InFlightDiagnostic()> EmitError,
                 llvm::ArrayRef<mlir::TypeAttr> Keys) {
  for (mlir::TypeAttr Key : Keys) {
    if (!Key || !isa<MemberNameType, TagType, PathType, MatchType>(
                    Key.getValue()))
      return EmitError() << "expecting a !nbdl.member_name, a !nbdl.tag, "
                            "a nested !nbdl.path, or a !nbdl.match "
                            "as a path key: " << Key;
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

namespace {
// Resolve the paths of stores for a get_path.
class StorePathResolver {
  GetPathOp Op;

  // Denote a key as a resolved type or as the value of a key operand.
  struct PendingKey {
    mlir::TypeAttr Type;
    mlir::Value Key;
  };

public:
  explicit StorePathResolver(GetPathOp Op) : Op(Op) { }

  // Get the root of Store and append the keys of its path to Keys
  // or return null on failure.
  mlir::Value resolve(mlir::Value Store,
                      llvm::SmallVectorImpl<mlir::TypeAttr>& Keys) {
    mlir::MLIRContext* Ctx = Op.getContext();
    // Collect keys from the store to the root.
    llvm::SmallVector<PendingKey, 4> Pending;
    mlir::Value V = Store;
    while (!isRoot(V)) {
      auto Arg = dyn_cast<mlir::BlockArgument>(V);
      mlir::Operation* Owner = Arg ? Arg.getOwner()->getParentOp()
                                   : V.getDefiningOp();
      if (isa_and_nonnull<LoopOp>(Owner))
        return fail("path of store cannot be resolved in recursive context");
      auto MatchLike = dyn_cast_or_null<nbdl_spec::MatchLike>(Owner);
      if (!MatchLike)
        return fail("unable to resolve the path of a store");

      // Key the alternative selected by a match overload of a single type.
      auto ST = dyn_cast<StoreType>(V.getType());
      if (Arg && isa<MatchOp>(Owner) && ST && ST.getAlts().size() == 1)
        Pending.push_back({mlir::TypeAttr::get(
            MatchType::get(Ctx, ST.getAlts().front())), {}});

      if (mlir::Value Aliased = MatchLike.getAliasedStore(V)) {
        V = Aliased;
        continue;
      }
      mlir::Value Parent = MatchLike.getParentStore(V);
      mlir::Value Key = getKey(Owner);
      if (!Parent || !Key)
        return fail("unable to resolve the key of a store in its parent");
      Pending.push_back({{}, Key});
      V = Parent;
    }

    for (PendingKey& P : llvm::reverse(Pending)) {
      mlir::TypeAttr Type = P.Type ? P.Type : resolveKey(P.Key, V);
      if (!Type)
        return {};
      Keys.push_back(Type);
    }
    return V;
  }

private:
  // Return true if V is a root store.
  static bool isRoot(mlir::Value V) {
    return isa_and_nonnull<LiftStoreOp, StoreComposeOp>(V.getDefiningOp());
  }

  // Get the key of a get or match or null if there is none.
  static mlir::Value getKey(mlir::Operation* Owner) {
    if (auto Get = dyn_cast<GetOp>(Owner))
      return Get.getKey();
    if (auto Match = dyn_cast<MatchOp>(Owner))
      return Match.getKey();
    return {};
  }

  // Get the path key of the value Key in a path from Root
  // or return null on failure.
  mlir::TypeAttr resolveKey(mlir::Value Key, mlir::Value Root) {
    mlir::Type T = Key.getType();
    if (isa<MemberNameType, TagType>(T))
      return mlir::TypeAttr::get(T);
    if (isa<StoreType>(T)) {
      llvm::SmallVector<mlir::TypeAttr, 4> Keys;
      mlir::Value KeyRoot = resolve(Key, Keys);
      if (!KeyRoot)
        return {};
      if (KeyRoot == Root)
        return mlir::TypeAttr::get(PathType::get(Op.getContext(), Keys));
    }
    fail("expecting a member name, a tag, or a store from the same root "
         "as a path key");
    return {};
  }

  // Emit Msg at the get_path and return null.
  mlir::Value fail(llvm::StringRef Msg) {
    Op.emitError(Msg);
    return {};
  }
};
} // namespace

mlir::FailureOr<PathType> nbdl_spec::resolveStorePath(GetPathOp Op) {
  llvm::SmallVector<mlir::TypeAttr, 4> Keys;
  if (!StorePathResolver(Op).resolve(Op.getStore(), Keys))
    return mlir::failure();
  return PathType::get(Op.getContext(), Keys);
}
