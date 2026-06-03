// Copyright Jason Rice 2026

#include <schir/SchirScheme.h>
#include <mlir/Target/LLVMIR/Export.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Plugins/PassPlugin.h>

namespace mlir {
  class ModuleOp;
}

// List of Modules to convert to LLVM IR.
static schir::ContextLocal InjectedModules;
// Push a mlir::ModuleOp to the list.
extern "C" void
schir_llvm_pass_inject_module(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  schir::Binding* B = ::InjectedModules.getBinding(C);
  schir::Value V = B->getValue();
  if (isa<schir::Undefined>(V))
    V = schir::Empty();

  schir::Value ValidModule;
  if (auto* Op = dyn_cast<mlir::Operation>(Args.front())) {
    if (isa<mlir::ModuleOp>(Op))
      ValidModule = Args.front();
  }
  if (!ValidModule)
    return C.RaiseError("expecting mlir module");
  // Validate the module.
  B->setValue(C.CreatePair(ValidModule, V));
  C.Cont();
}

namespace schir_clang {
// Access a static instance (that is defined in SchirClang).
extern schir::SchirScheme& getSchirSchemeInstance();
}

namespace {
class InjectPass : public llvm::PassInfoMixin<InjectPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module& MainModule,
                              llvm::ModuleAnalysisManager& MA) {
    llvm::LLVMContext& LLVMCtx = MainModule.getContext();
    schir::SchirScheme& SchirScheme = schir_clang::getSchirSchemeInstance();
    schir::Context& C = SchirScheme.getContext();
    schir::Value ModuleList = ::InjectedModules.get(C);
    for (schir::Value OpVal : ModuleList) {
      auto* Op = cast<mlir::Operation>(OpVal);
      if (mlir::isa<mlir::ModuleOp>(Op)) {
        std::unique_ptr<llvm::Module> M
          = mlir::translateModuleToLLVMIR(Op, LLVMCtx);
        if (!M || llvm::Linker::linkModules(MainModule, std::move(M))) {
          llvm::Linker::linkModules(MainModule, std::move(M));
        } else {
          LLVMCtx.emitError("Schir inject IR failed.");
          return llvm::PreservedAnalyses::all();
        }
      } else {
        LLVMCtx.emitError("SchirClang unsupported injected operation: ");
        OpVal.dump();
      }
    }

    return llvm::PreservedAnalyses::none();
  }

  constexpr static bool isRequired() { return true; }
};

void RegisterPassBuilderCallbacks(llvm::PassBuilder& PB) {
  PB.registerOptimizerEarlyEPCallback(
    [](llvm::ModulePassManager& MPM, llvm::OptimizationLevel,
       llvm::ThinOrFullLTOPhase) {
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
