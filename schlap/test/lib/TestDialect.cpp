#include "TestDialect.h"
#include <schir_common/Interfaces.h>

#include "TestDialect.cpp.inc"

#define GET_OP_CLASSES
#include "TestOps.cpp.inc"

void schlap_test::SchlapTestDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "TestOps.cpp.inc"
      >();
}

namespace {
// Get the value that V is read from through keyed reads and append the keys
// from V back to it, or return null if a read has no key.
mlir::Value getKeyedRoot(mlir::Value V,
                         llvm::SmallVectorImpl<mlir::StringAttr>& Keys) {
  while (auto Read = V.getDefiningOp<schlap_test::ReadOp>()) {
    if (!Read.getKeyAttr())
      return {};
    Keys.push_back(Read.getKeyAttr());
    V = Read.getFrom();
  }
  if (schir_common::getReadFrom(V))
    return {};
  return V;
}

// State that the result of a read is read from its operand, and that a
// value owns the results of the reads whose keys extend its own.
struct ReadAccess
    : schir_common::ValueAccess::ExternalModel<ReadAccess,
                                               schlap_test::ReadOp> {
  mlir::Value getReadFrom(mlir::Operation* Op, mlir::Value) const {
    return cast<schlap_test::ReadOp>(Op).getFrom();
  }

  bool mayOwn(mlir::Operation*, mlir::Value Owner, mlir::Value V) const {
    llvm::SmallVector<mlir::StringAttr> OwnerKeys, Keys;
    mlir::Value OwnerRoot = getKeyedRoot(Owner, OwnerKeys);
    mlir::Value Root = getKeyedRoot(V, Keys);
    if (!OwnerRoot || !Root)
      return V != Owner && schir_common::getReadFromRoot(V) ==
                           schir_common::getReadFromRoot(Owner);
    return Root == OwnerRoot && OwnerKeys.size() < Keys.size() &&
           llvm::equal(OwnerKeys, llvm::ArrayRef(Keys).take_back(
                                      OwnerKeys.size()));
  }
};

// State that the arguments of a branch are read from its operand.
struct BranchAccess
    : schir_common::ValueAccess::ExternalModel<BranchAccess,
                                               schlap_test::BranchOp> {
  mlir::Value getReadFrom(mlir::Operation* Op, mlir::Value) const {
    return cast<schlap_test::BranchOp>(Op).getFrom();
  }
};

// State that a mutate mutates each operand.
struct MutateAccess
    : schir_common::ValueAccess::ExternalModel<MutateAccess,
                                               schlap_test::MutateOp> {
  bool isMutated(mlir::Operation*, mlir::OpOperand&) const { return true; }
};

// State that a forget uses no operand.
struct ForgetAccess
    : schir_common::ValueAccess::ExternalModel<ForgetAccess,
                                               schlap_test::ForgetOp> {
  bool isUsed(mlir::Operation*, mlir::OpOperand&) const { return false; }
};
}  // namespace

void schlap_test::registerTestDialect(mlir::DialectRegistry& Registry) {
  Registry.insert<SchlapTestDialect>();
  Registry.addExtension(+[](mlir::MLIRContext* Context, SchlapTestDialect*) {
    ReadOp::attachInterface<ReadAccess>(*Context);
    BranchOp::attachInterface<BranchAccess>(*Context);
    MutateOp::attachInterface<MutateAccess>(*Context);
    ForgetOp::attachInterface<ForgetAccess>(*Context);
  });
}
