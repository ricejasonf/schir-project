// Copyright Jason Rice 2025

#include <schir/SchirScheme.h>
#include <llvm/IR/PassManager.h>

namespace schir_clang {
// Get the static instance from the parser plugin... :(
extern schir::SchirScheme& getSchirSchemeInstance();
extern schir::ContextLocal // what was this going to be?
}

namespace {
class InjectPass : llvm::PassInfoMixin<InjectPass> {
  llvm::PreservedAnalyses run(llvm::Module& MainModule,
                              llvm::ModuleAnalysesManager& MA) {
    llvm::LLVMContext& LLVMCtx = MainModule.getContext();
    schir::SchirScheme& SchirScheme = getSchirSheme();
    schir::Context& C = getSchirScheme().getContext();
    schir::Value Modules = schir_clang::RegisteredModules.get(C);
    for (schir::Value Module : Modules) {
      auto* Op = cast<mlir::Operation>(Module);
      if (auto* ModuleOp = dyn_cast<mlir::ModuleOp>(Module)) {
        std::unique_ptr<llvm::Module> M
          = mlir::translateModuleToLLVMIR(ModuleOp, LLVMCtx);
        if (!M || llvm::Linker::linkModules(MainModule, std::move(M))) {
          llvm::Linker::linkModules(MainModule, std::move(M));
        } else {
          LLVMCtx.emitError("Schir inject IR failed.");
          return llvm::PreseveredAnalyses::all();
        }
      } else {
        // TODO We could possibly support user provided llvm::Module here.
        LLVMCtx.emitError("SchirClang unexpected value kind for Module: ");
        Module.dump();
      }
    }

    return llvm::PreservedAnalyses::none();
  }

  constexpr static bool isRequired() { return true; }
};

void RegisterPassBuilderCallbacks(llvm::PassBuilder& PB) {
  PB.registerOptimizerStartEPCallback(
    [](llvm::ModulePassManager& MPM, llvm::OptimizationLevel) {
      MPM.addPass(InjectPass());
    });
}
} // namespace

// LLVM Pass Plugin entrypoint
extern "C" llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {.APIVersion = LLVM_PLUGIN_API_VERSION,
          .PluginName = "SchirInject",
          .PluginVersion = "v0.1",
          .RegisterPassBuilderCallbacks = RegisterPassBuilderCallbacks};
}
