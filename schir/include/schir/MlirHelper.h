#ifndef SCHIR_MLIR_HELPER_H
#define SCHIR_MLIR_HELPER_H

#include <schir/Context.h>
#include <schir/Mlir.h>
#include <schir/Value.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/Value.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/Support/LogicalResult.h>

namespace mlir {
  class OpBuilder;
  class Operation;
}

namespace schir::mlir_bind_var {
extern schir::ContextLocal current_context;
extern schir::ContextLocal current_builder;
}

namespace schir::mlir_helper {
mlir::MLIRContext* getCurrentContext(schir::Context& C);
mlir::OpBuilder* getBuilder(schir::Context& C, schir::Value V);
mlir::OpBuilder* getCurrentBuilder(schir::Context& C);
mlir::Operation* getSingleOpArg(schir::Context& C, schir::ValueRefs Args);
void WithBuilderImpl(Context& C, mlir::OpBuilder const& Builder,
                     schir::Value Thunk);

// Call a stack Thunk capturing any diagnostics and emitting them as
// notes to an error raised in the schir::Context with ErrorMsg and
// an optional irritant should Thunk return llvm::failure().
// Return the result of Thunk.
llvm::LogicalResult WithDiagnosticsHandler(
                               schir::Context& C,
                               llvm::function_ref<llvm::LogicalResult()> Thunk,
                               llvm::StringRef ErrorMsg,
                               schir::Value Irr = schir::Undefined());
} // namespace schir::mlir_helper


#endif // SCHIR_MLIR_HELPER_H
