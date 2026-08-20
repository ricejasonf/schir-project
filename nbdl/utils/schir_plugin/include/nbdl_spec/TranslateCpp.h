// Copyright 2026 Jason Rice

#ifndef NBDL_SPEC_NBDL_TRANSLATE_CPP_H
#define NBDL_SPEC_NBDL_TRANSLATE_CPP_H

#include <nbdl_spec/NbdlDialect.h>
#include <schir/Source.h>
#include <schir/Value.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/Support/raw_ostream.h>
#include <mlir/IR/Value.h>

namespace nbdl_spec {

std::tuple<std::string, schir::SourceLocationEncoding*, mlir::Operation*>
translate_cpp(schir::LexerWriterFnRef FnRef, mlir::Operation* Op);

// Provide customization point for writing subexpressions
using WriteExprFn = llvm::function_ref<void(mlir::Value)>;

// Write the RHS of a nbdl.visit.
void writeVisitExpr(nbdl_spec::VisitOp Op, llvm::raw_ostream& OS,
                    WriteExprFn WriteExpr);

} // namespace nbdl_spec

#endif // NBDL_SPEC_NBDL_TRANSLATE_CPP_H
