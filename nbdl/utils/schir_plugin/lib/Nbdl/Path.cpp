// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Implement the !nbdl.path type and its static comparisons.
//
#include <nbdl_spec/NbdlDialect.h>
#include <schir_common/Interfaces.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <optional>

using namespace nbdl_spec;

// Verify the keys of a path, where the unit type is only valid
// as the only key of the path of a root.
llvm::LogicalResult
PathType::verify(llvm::function_ref<mlir::InFlightDiagnostic()> EmitError,
                 llvm::ArrayRef<mlir::TypeAttr> Keys) {
  if (Keys.size() == 1 && Keys.front() &&
      isa<UnitType>(Keys.front().getValue()))
    return mlir::success();
  for (mlir::TypeAttr Key : Keys) {
    if (!Key || !isa<MemberNameType, TagType, PathType, MatchType>(
                    Key.getValue()))
      return EmitError() << "expecting a !nbdl.member_name, a !nbdl.tag, "
                            "a nested !nbdl.path, or a !nbdl.match "
                            "as a path key: " << Key;
    if (auto Nested = dyn_cast<PathType>(Key.getValue());
        Nested && !Nested.isResolved())
      return EmitError() << "expecting a resolved nested !nbdl.path";
  }
  return mlir::success();
}

// Return true if the keys may be equal at runtime.
static bool mayEqualPathKeys(mlir::TypeAttr A, mlir::TypeAttr B) {
  return A == B || (isa<PathType>(A.getValue()) &&
                    isa<PathType>(B.getValue()));
}

// Return true if Prefix may resolve to a prefix of Keys.
static bool isKeyPrefix(llvm::ArrayRef<mlir::TypeAttr> Prefix,
                        llvm::ArrayRef<mlir::TypeAttr> Keys) {
  if (Prefix.size() > Keys.size())
    return false;
  for (auto [A, B] : llvm::zip(Prefix, Keys))
    if (!mayEqualPathKeys(A, B))
      return false;
  return true;
}

bool nbdl_spec::isPathPrefix(PathType Prefix, PathType P) {
  return isKeyPrefix(Prefix.getKeys(), P.getKeys());
}

bool nbdl_spec::mayAliasPaths(PathType A, PathType B) {
  return isPathPrefix(A, B) || isPathPrefix(B, A);
}

// Get the path of Keys from a root, which is the unit key if it is empty.
static PathType getPath(mlir::MLIRContext* Ctx,
                        llvm::ArrayRef<mlir::TypeAttr> Keys) {
  if (Keys.empty())
    return PathType::get(Ctx, mlir::TypeAttr::get(UnitType::get(Ctx)));
  return PathType::get(Ctx, Keys);
}

namespace {
// Resolve the paths of stores for a get_path.
class StorePathResolver {
  mlir::Operation* Op;
  bool Diagnose;

  // Denote a key as a resolved type, as the value of a key operand,
  // or as neither for a match overload that is not resolved.
  struct PendingKey {
    mlir::TypeAttr Type;
    mlir::Value Key;
  };

public:
  StorePathResolver(mlir::Operation* Op, bool Diagnose)
    : Op(Op), Diagnose(Diagnose) { }

  // Get the root of Store and append the keys of its path to Keys
  // or return null on failure.
  mlir::Value resolve(mlir::Value Store,
                      llvm::SmallVectorImpl<mlir::TypeAttr>& Keys) {
    mlir::MLIRContext* Ctx = Op->getContext();
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

      // Key the alternative selected by a match overload of a single type
      // unless it is the only alternative.
      if (auto Match = dyn_cast<MatchOp>(Owner); Arg && Match) {
        auto ST = dyn_cast<StoreType>(V.getType());
        std::optional<unsigned> NumAlts = countMatchAlts(Match);
        if (!ST || ST.getAlts().size() != 1 || !NumAlts) {
          Pending.push_back({});
        } else if (*NumAlts > 1) {
          Pending.push_back({mlir::TypeAttr::get(
              MatchType::get(Ctx, ST.getAlts().front())), {}});
        }
      }

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

    // Report an unresolved match overload only if every key resolves.
    bool HasUnresolvedMatch = false;
    for (PendingKey& P : llvm::reverse(Pending)) {
      if (!P.Type && !P.Key) {
        HasUnresolvedMatch = true;
        continue;
      }
      mlir::TypeAttr Type = P.Type ? P.Type : resolveKey(P.Key, V);
      if (!Type)
        return {};
      Keys.push_back(Type);
    }
    // TODO See if this case is possible.
    if (HasUnresolvedMatch)
      return fail("unable to resolve the type of a match overload");
    return V;
  }

private:
  // Return true if V is a root store.
  static bool isRoot(mlir::Value V) {
    return isa_and_nonnull<LiftStoreOp, StoreComposeOp>(V.getDefiningOp());
  }

  // Count the alternatives of the element that Match selects from
  // or return nothing if an overload is not resolved.
  static std::optional<unsigned> countMatchAlts(MatchOp Match) {
    llvm::SmallPtrSet<mlir::Attribute, 4> Alts;
    for (mlir::Region& Overload : Match.getOverloads()) {
      if (Overload.getNumArguments() != 1)
        return std::nullopt;
      auto ST = dyn_cast<StoreType>(Overload.getArgument(0).getType());
      if (!ST || ST.getAlts().empty())
        return std::nullopt;
      for (mlir::TypeAttr Alt : ST.getAlts())
        Alts.insert(Alt);
    }
    return Alts.size();
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
        return mlir::TypeAttr::get(getPath(Op->getContext(), Keys));
    }
    fail("expecting a member name, a tag, or a store from the same root "
         "as a path key");
    return {};
  }

  // Emit Msg at the get_path if diagnosing and return null.
  mlir::Value fail(llvm::StringRef Msg) {
    if (Diagnose)
      Op->emitError(Msg);
    return {};
  }
};
} // namespace

mlir::FailureOr<PathType> nbdl_spec::resolveStorePath(mlir::Operation* Op,
                                                      mlir::Value Store,
                                                      bool Diagnose) {
  llvm::SmallVector<mlir::TypeAttr, 4> Keys;
  if (!StorePathResolver(Op, Diagnose).resolve(Store, Keys))
    return mlir::failure();
  return getPath(Op->getContext(), Keys);
}

// Return true if the root stores may be the same object.
static bool mayEqualRoots(mlir::Value A, mlir::Value B) {
  if (A == B)
    return true;
  auto LiftA = A.getDefiningOp<LiftStoreOp>();
  auto LiftB = B.getDefiningOp<LiftStoreOp>();
  return LiftA && LiftB && LiftA.getValue() == LiftB.getValue();
}

bool nbdl_spec::mayOwnStore(mlir::Operation* Op, mlir::Value Owner,
                            mlir::Value Store) {
  llvm::SmallVector<mlir::TypeAttr, 4> OwnerKeys, Keys;
  auto Resolver = StorePathResolver(Op, false);
  mlir::Value OwnerRoot = Resolver.resolve(Owner, OwnerKeys);
  mlir::Value Root = Resolver.resolve(Store, Keys);
  if (OwnerRoot && Root)
    return mayEqualRoots(OwnerRoot, Root) &&
           OwnerKeys.size() < Keys.size() && isKeyPrefix(OwnerKeys, Keys);
  // Fall back to every value read from the same root but Owner itself.
  return Store != Owner &&
         mayEqualRoots(schir_common::getReadFromRoot(Owner),
                       schir_common::getReadFromRoot(Store));
}

PathType nbdl_spec::getMatchPath(MatchOp Op) {
  if (Op.hasUnitKey())
    return {};
  return dyn_cast<PathType>(Op.getKey().getType());
}
