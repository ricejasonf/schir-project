//===--- Dialect.cpp - SchirScheme MLIR Dialect Registration --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations for SchirScheme's MLIR Dialect
//
//===----------------------------------------------------------------------===//

#include "schir/Context.h"
#include "schir/Dialect.h"
#include "schir/Lexer.h"
#include "schir/Parser.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/TypeUtilities.h"
#include "llvm/ADT/TypeSwitch.h"
#include <string>

using namespace schir;

#include "schir/Dialect/SchirDialect.cpp.inc"

void SchirDialect::initialize() {
  //addTypes<SchirOpGenType>();
  //addTypes<SchirMlirValueType>();

  addTypes<
#define GET_TYPEDEF_LIST
#include "schir/Dialect/SchirTypes.cpp.inc"
    >();

  addAttributes<
#define GET_ATTRDEF_LIST
#include "schir/Dialect/SchirAttrs.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "schir/Dialect/SchirOps.cpp.inc"
    >();
}

mlir::Attribute SchirDialect::parseAttribute(mlir::DialectAsmParser& P,
                                             mlir::Type type) const {
  // Parse SchirValueAttr.
  std::string string;
  if (P.parseString(&string))
    return {};
  mlir::MLIRContext* MlirCtx = P.getContext();
  auto StringAttr = mlir::StringAttr::get(MlirCtx, string);
  return SchirValueAttr::get(MlirCtx, StringAttr);
}

void SchirDialect::printAttribute(
                        mlir::Attribute Attr,
                        mlir::DialectAsmPrinter& P) const {
  // All attributes are SchirValueAttr
  P.printAttribute(mlir::cast<SchirValueAttr>(Attr).getExpr());
}

void BindingOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                      mlir::Value Input) {
  BindingOp::build(B, OpState, B.getType<SchirBindingType>(), Input);
}

void ConsOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                   mlir::Value X, mlir::Value Y) {
  mlir::Type ResultType = B.getType<SchirPairType>();
  ConsOp::build(B, OpState, ResultType, X, Y);
}

void IfOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                 mlir::Value Input) {
  IfOp::build(B, OpState, B.getType<SchirValueType>(), Input);
}

void LambdaOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                     llvm::StringRef Name,
                     llvm::ArrayRef<mlir::Value> Captures) {
  mlir::Type ResultType = B.getType<SchirProcedureType>();
  LambdaOp::build(B, OpState, ResultType, Name, Captures);
}

void LiteralOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                      schir::Value V) {
  assert(!isa<SyntaxClosure>(V));
  // create a SchirValueAttr from schir::Value
  LiteralOp::build(B, OpState, B.getType<SchirValueType>(),
                   SchirValueAttr::get(B.getContext(), V));
}

void LoadRefOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                          mlir::Value ValueRefs, uint32_t Index) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  LoadRefOp::build(B, OpState, ResultType, ValueRefs,
                       B.getUI32IntegerAttr(Index));
}

void LoadGlobalOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                         llvm::StringRef SymName) {
  mlir::Type ResultType = B.getType<SchirUnknownType>();
  LoadGlobalOp::build(B, OpState, ResultType, SymName);
}

void LoadModuleOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                         llvm::StringRef SymName) {
  LoadModuleOp::build(B, OpState, {}, SymName);
}

void MatchOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                    schir::Value val, mlir::Value input) {
  assert((!isa<Pair, Vector>(val)) && "expected non-structural constant");
  auto ValAttr = SchirValueAttr::get(B.getContext(), val);
  MatchOp::build(B, OpState, ValAttr, input);
}

void MatchPairOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        mlir::Value input) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  MatchPairOp::build(B, OpState,
                     /*car=*/ResultType,
                     /*cdr=*/ResultType,
                     input);
}

void MatchTailOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        uint32_t Length, mlir::Value Input) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  MatchTailOp::build(B, OpState, ResultType,
                     Length, Input);
}

void MatchArgsOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        mlir::FunctionType FT, mlir::Value Input) {
  assert((!FT.getInputs().empty() &&
          isa<SchirContextType>(FT.getInputs().front())) &&
    "expecting schir-scheme function type");
  // Drop the context argument.
  auto ResultTypes = FT.getInputs().drop_front();
  MatchArgsOp::build(B, OpState, ResultTypes, Input);
}

void MatchVectorOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                          uint32_t Head, uint32_t Tail, mlir::Value Input) {
  llvm_unreachable("FIXME This is all very untested.");
  assert(Tail >= Head);
  unsigned NumResults = Tail - Head;
  mlir::Type ResultType = B.getType<SchirValueType>();
  llvm::SmallVector<mlir::Type, 4> ResultTypes(NumResults, ResultType);
  MatchVectorOp::build(B, OpState, ResultTypes, Head, Tail, Input);
}

void SubpatternOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        mlir::Value Input, mlir::Value Tail,
                        std::unique_ptr<mlir::Region>&& Body,
                        unsigned NumPacks) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  llvm::SmallVector<mlir::Type, 4> ResultTypes(NumPacks, ResultType);
  OpState.addOperands({Input, Tail});
  OpState.addRegion(std::move(Body));
  OpState.addTypes(std::move(ResultTypes));
}

void ExpandPacksOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        mlir::Value Cdr, mlir::ValueRange Packs,
                        std::unique_ptr<mlir::Region>&& Body) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  OpState.addOperands(Cdr);
  OpState.addOperands(Packs);
  OpState.addRegion(std::move(Body));
  OpState.addTypes(ResultType);
}

void RenameOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                     llvm::StringRef Id, mlir::Value Capture) {
  mlir::Type ResultType = B.getType<SchirBindingType>();
  RenameOp::build(B, OpState, ResultType, Id, Capture);
}

void RenameGlobalOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                     llvm::StringRef Id, llvm::StringRef Sym) {
  mlir::Type ResultType = B.getType<SchirUnknownType>();
  RenameGlobalOp::build(B, OpState, ResultType, Id, Sym);
}

void SetOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                     mlir::Value Binding, mlir::Value Input) {
  SetOp::build(B, OpState, B.getType<SchirValueType>(), Binding, Input);
}

void SpliceOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                   mlir::Value X, mlir::Value Y) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  SpliceOp::build(B, OpState, ResultType, X, Y);
}

void SyntaxClosureOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                            mlir::Value SourceVal, mlir::Value Input,
                            mlir::Value Env) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  SyntaxClosureOp::build(B, OpState, ResultType, SourceVal, Input, Env);
}

void SyntaxOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                    llvm::StringRef MangledName) {
  mlir::Type SchirSyntaxT = B.getType<SchirSyntaxType>();
  SyntaxOp::build(B, OpState, SchirSyntaxT, MangledName);
}

void ToVectorOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                            mlir::Value input) {
  mlir::Type ResultType = B.getType<SchirVectorType>();
  ToVectorOp::build(B, OpState, ResultType, input);
}

void VectorOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                            llvm::ArrayRef<mlir::Value> args) {
  mlir::Type ResultType = B.getType<SchirVectorType>();
  VectorOp::build(B, OpState, ResultType, args);
}

void RenameEnvOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                    mlir::Value Env, llvm::ArrayRef<mlir::Value> Bindings) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  RenameEnvOp::build(B, OpState, ResultType, Env, Bindings);
}

void UndefinedOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState) {
  UndefinedOp::build(B, OpState, B.getType<SchirUndefinedType>());
}

void SourceLocOp::build(mlir::OpBuilder& B, mlir::OperationState& OpState,
                        mlir::Value Arg) {
  mlir::Type ResultType = B.getType<SchirValueType>();
  SourceLocOp::build(B, OpState, ResultType, Arg);
}

using namespace mlir;
#define GET_OP_CLASSES
#include "schir/Dialect/SchirOps.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "schir/Dialect/SchirAttrs.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "schir/Dialect/SchirTypes.cpp.inc"

namespace schir::detail {
// Manually define SchirValueAttrStorage to cache the parsed
// scheme expression.
struct SchirValueAttrStorage : mlir::AttributeStorage {
  using KeyTy = std::tuple<StringAttr>;
  SchirValueAttrStorage(StringAttr expr) : expr(std::move(expr)) {}

  KeyTy getAsKey() const {
    return KeyTy(expr);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (expr == std::get<0>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey));
  }

  static SchirValueAttrStorage *construct(
      mlir::AttributeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto expr = std::move(std::get<0>(tblgenKey));
    return new (allocator.allocate<SchirValueAttrStorage>())
               SchirValueAttrStorage(std::move(expr));
  }

  StringAttr expr;

  // Cache the result of parsing the expr.
  // This requires visitation by the garbage collector.
  schir::Value Val = nullptr;
};
}  // namespace schir::detail

mlir::StringAttr SchirValueAttr::getExpr() const {
  return getImpl()->expr;
}
schir::Value& SchirValueAttr::getCachedValue() {
  return getImpl()->Val;
}

schir::Value SchirValueAttr::getValue(schir::Context& C) const {
  schir::Value& Val = getImpl()->Val;
  if (!Val)
    Val = C.ParseLiteral(getImpl()->expr);
  return Val;
}

SchirValueAttr SchirValueAttr::get(mlir::MLIRContext* Ctx,
                                   schir::Value V) {
  // Write the value to a string.
  std::string string;
  llvm::raw_string_ostream stream(string);
  schir::write(stream, V);
  mlir::StringAttr StringAttr = mlir::StringAttr::get(Ctx, string);
  SchirValueAttr Attr = get(Ctx, StringAttr);
  Attr.getCachedValue() = V;
  return Attr;
}

