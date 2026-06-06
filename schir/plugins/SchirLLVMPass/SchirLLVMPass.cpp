// Copyright Jason Rice 2026

#include <schir/SchirScheme.h>
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include <mlir/Target/LLVMIR/Export.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
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
schir_llvm_pass_init(schir::Context& C, schir::ValueRefs Args) {
  if (!C.MLIRContext)
    return C.RaiseError("MLIRContext is not set");
  // Ensure the LLVM Dialect is loaded.
  C.MLIRContext->getOrLoadDialect<mlir::LLVM::LLVMDialect>();
  mlir::registerBuiltinDialectTranslation(*C.MLIRContext);
  mlir::registerLLVMDialectTranslation(*C.MLIRContext);
  C.Cont();
}

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
extern schir::SchirScheme* getSchirSchemeInstance();
}

namespace {
class DiagnosticHandlerHolder {
  llvm::LLVMContext& LLVMCtx;
  std::unique_ptr<llvm::DiagnosticHandler> PrevHandler;
public:
  DiagnosticHandlerHolder(llvm::LLVMContext& LLVMCtx)
    : LLVMCtx(LLVMCtx),
      PrevHandler(LLVMCtx.getDiagnosticHandler())
  { }

  ~DiagnosticHandlerHolder() {
    LLVMCtx.setDiagnosticHandler(std::move(PrevHandler));
  }
};

class InjectPass : public llvm::PassInfoMixin<InjectPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module& MainModule,
                              llvm::ModuleAnalysisManager& MA) {
    schir::SchirScheme* SchirScheme = schir_clang::getSchirSchemeInstance();
    if (!SchirScheme)
      return llvm::PreservedAnalyses::all();
    schir::Context& C = SchirScheme->getContext();
    llvm::LLVMContext& LLVMCtx = MainModule.getContext();
    // Hijack the DiagnosticHandler temporarily.
    // It prints to llvm::errs() by default.
    DiagnosticHandlerHolder DHH(LLVMCtx);
    // If ModuleList is not defined or is empty just skip.
    schir::Value ModuleList = ::InjectedModules.get(C);
    if (!isa<schir::Pair>(ModuleList))
      return llvm::PreservedAnalyses::all();
    for (schir::Value OpVal : ModuleList) {
      auto* Op = cast<mlir::Operation>(OpVal);
      if (mlir::isa<mlir::ModuleOp>(Op)) {
        std::unique_ptr<llvm::Module> M
          = mlir::translateModuleToLLVMIR(Op, LLVMCtx);
        if (!M) {
          LLVMCtx.emitError("Schir mlir translate to llvm failed.");
          return llvm::PreservedAnalyses::all();
        } else {
          M->setDataLayout(MainModule.getDataLayout());
          M->setTargetTriple(MainModule.getTargetTriple());
          if (llvm::Linker::linkModules(MainModule, std::move(M))) {
            LLVMCtx.emitError("Schir llvm link module failed.");
            return llvm::PreservedAnalyses::all();
          }
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
  PB.registerPipelineStartEPCallback(
    [](llvm::ModulePassManager& MPM, llvm::OptimizationLevel/*,
       llvm::ThinOrFullLTOPhase*/) {
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
