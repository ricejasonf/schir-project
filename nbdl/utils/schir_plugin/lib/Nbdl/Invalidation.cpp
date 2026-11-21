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
// store it is a view of or else from its parent, and that it may be owned
// by the stores whose paths it extends.
template <typename Op>
struct MatchLikeAccess
    : schir_common::ValueAccess::ExternalModel<MatchLikeAccess<Op>, Op> {
  mlir::Value getReadFrom(mlir::Operation* O, mlir::Value Store) const {
    auto MatchLike = cast<nbdl_spec::MatchLike>(O);
    if (mlir::Value Aliased = MatchLike.getAliasedStore(Store))
      return Aliased;
    return MatchLike.getParentStore(Store);
  }

  bool mayOwn(mlir::Operation* O, mlir::Value Owner, mlir::Value Store) const {
    return mayOwnStore(O, Owner, Store);
  }
};

// Get the index into the args of a visit of an operand or -1 for the callee.
int32_t getVisitArgIndex(VisitOp Visit, mlir::OpOperand& Operand) {
  if (&Operand == &Visit.getFnMutable())
    return -1;
  return Operand.getOperandNumber() - Visit.getArgs().getBeginOperandIndex();
}

// State that a visit mutates the arguments listed in mutArgs and
// childStableMutArgs, leaving the stores read from the latter valid.
struct VisitAccess
    : schir_common::ValueAccess::ExternalModel<VisitAccess, VisitOp> {
  bool isMutated(mlir::Operation* O, mlir::OpOperand& Operand) const {
    auto Visit = cast<VisitOp>(O);
    int32_t ArgIndex = getVisitArgIndex(Visit, Operand);
    return llvm::is_contained(Visit.getMutArgs(), ArgIndex) ||
           llvm::is_contained(Visit.getChildStableMutArgs(), ArgIndex);
  }

  bool isChildStable(mlir::Operation* O, mlir::OpOperand& Operand) const {
    auto Visit = cast<VisitOp>(O);
    return llvm::is_contained(Visit.getChildStableMutArgs(),
                              getVisitArgIndex(Visit, Operand));
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
