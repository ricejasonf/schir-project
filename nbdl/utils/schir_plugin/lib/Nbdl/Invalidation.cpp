// Copyright Jason Rice 2026
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// State how nbdl operations access stores with the ValueAccess interface.
//
#include <nbdl_spec/NbdlDialect.h>
#include <schir_common/Interfaces.h>

using namespace nbdl_spec;

namespace {
// State that a store introduced by a MatchLike operation is read from the
// store it is a view of or else from its parent.
template <typename Op>
struct MatchLikeAccess
    : schir_common::ValueAccess::ExternalModel<MatchLikeAccess<Op>, Op> {
  mlir::Value getReadFrom(mlir::Operation* O, mlir::Value Store) const {
    auto MatchLike = cast<nbdl_spec::MatchLike>(O);
    if (mlir::Value Aliased = MatchLike.getAliasedStore(Store))
      return Aliased;
    return MatchLike.getParentStore(Store);
  }
};

// State that a mut visit mutates its store arguments.
struct VisitAccess
    : schir_common::ValueAccess::ExternalModel<VisitAccess, VisitOp> {
  bool isMutated(mlir::Operation* O, mlir::OpOperand& Operand) const {
    auto Visit = cast<VisitOp>(O);
    return Visit.getMut() && &Operand != &Visit.getFnMutable() &&
           isa<StoreType>(Operand.get().getType());
  }
};

// Attach MatchLikeAccess to each of Ops.
template <typename... Ops>
void attachMatchLikeAccess(mlir::MLIRContext& Context) {
  (Ops::template attachInterface<MatchLikeAccess<Ops>>(Context), ...);
}
}  // namespace

void nbdl_spec::registerValueAccess(mlir::DialectRegistry& Registry) {
  Registry.addExtension(+[](mlir::MLIRContext* Context, NbdlDialect*) {
    attachMatchLikeAccess<GetOp, MatchOp, MatchIfOp, MatchEachOp, ConstOp,
                          LoopOp>(*Context);
    VisitOp::attachInterface<VisitAccess>(*Context);
  });
}
