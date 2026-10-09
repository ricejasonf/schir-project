#ifndef NBDL_SPEC_NBDL_DIALECT_H
#define NBDL_SPEC_NBDL_DIALECT_H

#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Dialect.h>
#include <mlir/IR/OpDefinition.h>
#include <mlir/Interfaces/ControlFlowInterfaces.h>
#include <mlir/Interfaces/SideEffectInterfaces.h>
#include <schir/Interfaces/Interfaces.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>

namespace schir {
class SchirClangImpl;
}

namespace nbdl_spec {
using mlir::func::CallOp;
using mlir::func::FuncOp;
using mlir::StringAttr;

llvm::LogicalResult runFlattenPass(mlir::Operation* Op,
        schir::SchirClangImpl* SchirClangImpl = nullptr);

// Run the flatten pass with the passes of the given pipeline strings
// until a fixed point is reached. It is an error if the result type
// of any function is not inferred (ie it is still a placeholder.)
llvm::LogicalResult runInferencePasses(mlir::Operation* Op,
        schir::SchirClangImpl* SchirClangImpl,
        llvm::ArrayRef<std::string> Pipelines);

// Replace each !nbdl.cpp_alias with the !nbdl.cpp type
// of its canonical typename. (The flatten pass also does this.)
llvm::LogicalResult canonicalizeCppTypes(mlir::Operation* Op,
        schir::SchirClangImpl* SchirClangImpl);

// Register passes that can be run via a pass pipeline string.
void registerPasses();

// The attribute that marks a function to be called from C++ via a
// C adapter created by the nbdl-c-adapter pass. Its value is the
// function type before lowering.
inline constexpr llvm::StringLiteral CAdapterAttrName = "nbdl.c_adapter";

// Get the C linkage name of the adapter for a function lowered from MLIR
// given its symbol name.
// Each name component is prefixed with its length.
//  e.g. ::foo::add_i32 -> nbdl_c_3foo7add_i32
std::string getCAdapterName(llvm::StringRef SymName);
}

// Include the generated header files
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include "nbdl_spec/NbdlDialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "nbdl_spec/NbdlTypes.h.inc"

#define GET_ATTRDEF_CLASSES
#include "nbdl_spec/NbdlAttrs.h.inc"

#define GET_OP_CLASSES
#include "nbdl_spec/NbdlOps.h.inc"
#pragma clang diagnostic pop

#endif
