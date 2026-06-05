// Copyright Jason Rice 2026

#include <mlir/InitAllPasses.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Pass/PassRegistry.h>
#include <schir/Context.h>
#include <schir/MlirHelper.h>

extern "C" {
// This only calls the mlir::registerAllPasses function.
void schir_mlir_all_passes_register_all_passes(
                            schir::Context& C, schir::ValueRefs Args) {
  if (!Args.empty())
    return C.RaiseError("invalid arity");
  mlir::registerAllPasses();

  return C.Cont();
}

// Dynamically run a pass using a pipeline string like on the CLI.
// (run-passes module "str1" "str2" ...)
void schir_mlir_all_passes_run_passes(
                            schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() < 2)
    return C.RaiseError("invalid arity");
  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args.front());
  if (!Op)
    return C.RaiseError("expecting mlir.operation: {}", Args.front());
  Args = Args.drop_front();

  mlir::MLIRContext* MCtx = schir::mlir_helper::getCurrentContext(C);
  if (!MCtx)
    return C.RaiseError("mlir context not set");
  mlir::PassManager PM(MCtx);

  for (schir::Value Arg : Args) {
    llvm::StringRef PipelineStr = Arg.getStringRef();
    if (PipelineStr.empty())
      return C.RaiseError("expecting nonempty string-like: {}", Arg);

    std::string ErrString;
    llvm::raw_string_ostream ErrStream(ErrString);
    if (mlir::failed(mlir::parsePassPipeline(PipelineStr, PM, ErrStream))) {
      schir::String* ErrMsg = C.CreateString("Failed to parse pass pipeline: ",
                                             llvm::StringRef(ErrString));
      return C.RaiseError(ErrMsg, Arg);
    }
  }

  if (mlir::failed(PM.run(Op)))
    return C.RaiseError("mlir pass pipeline failed");

  return C.Cont();
}
}
