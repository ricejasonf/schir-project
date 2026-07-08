// Copyright Jason Rice 2026

#include <schir/Context.h>
#include <schir/Mangle.h>
#include <llvm/ADT/SmallString.h>
#include <string>


using Context = schir::Context;
using ValueRefs = schir::ValueRefs;
// Module prefix is the mangled library name
// prepended with the mangle prefix.
schir::ContextLocal current_module_prefix;

using RunMangleFn = std::string(schir::Mangler&,
                                llvm::StringRef,
                                schir::Value Arg);

void RunMangleFunc(Context& C, ValueRefs Args,
                   llvm::function_ref<RunMangleFn> RunMangle) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  llvm::StringRef Prefix = Args.front().getStringRef();
  if (Prefix.empty())
    return C.RaiseError("invalid prefix argument: {}", Args.front());
  Args = Args.drop_front();
  schir::Mangler M(C);
  std::string Result = RunMangle(M, Prefix, Args.front());
  if (Result.empty())
    return; // Error raised by Mangler
  C.Cont(C.CreateString(Result));
}


extern "C" {
// Mangle the library spec to create the ModulePrefix.
// (mangle-module ManglePrefix LibrarySpec)
void schir_mangle_module(Context& C, ValueRefs Args) {
  RunMangleFunc(C, Args, [](schir::Mangler& M, llvm::StringRef ManglePrefix,
                            schir::Value V) {
    return M.mangleModule(ManglePrefix, V);
  });
}

// (mangle-variable ModulePrefix VarName)
void schir_mangle_variable_name(Context& C, ValueRefs Args) {
  RunMangleFunc(C, Args, [](schir::Mangler& M, llvm::StringRef ModulePrefix,
                            schir::Value V) {
    return M.mangleVariable(ModulePrefix, V);
  });
}

// (mangle-function ModulePrefix FuncName)
void schir_mangle_function_name(Context& C, ValueRefs Args) {
  RunMangleFunc(C, Args, [](schir::Mangler& M, llvm::StringRef ModulePrefix,
                            schir::Value V) {
    return M.mangleFunction(ModulePrefix, V);
  });
}

// Get the original library spec to possibly
// generate a file path for importing a module.
void schir_parse_library_spec(Context& C, ValueRefs Args) {
  C.RaiseError("TODO implement parse-library-spec");
}

// Get the module prefix to compare to see if an
// entity is external to some other module.
void schir_parse_module_prefix(Context& C, ValueRefs Args) {
  C.RaiseError("TODO implement parse-module-prefix");
}

} // extern "C"
