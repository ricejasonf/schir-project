// Copyright Jason Rice 2025
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Include all of the generated header/source files for the nbdl mlir dialect.
// 
#include <nbdl_spec/NbdlDialect.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/OpImplementation.h>
#include <mlir/Transforms/InliningUtils.h>
#include <llvm/ADT/TypeSwitch.h>

// Include generated source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include "nbdl_spec/NbdlDialect.cpp.inc"
#include "nbdl_spec/NbdlInterfaces.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "nbdl_spec/NbdlTypes.cpp.inc"
#define GET_ATTRDEF_CLASSES
#include "nbdl_spec/NbdlAttrs.cpp.inc"
#define GET_OP_CLASSES
#include "nbdl_spec/NbdlOps.cpp.inc"

namespace {
struct NbdlInlinerInterface : mlir::DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  bool isLegalToInline(mlir::Operation* Op, mlir::Region*, bool,
                       mlir::IRMapping&) const final {
    return llvm::isa<nbdl_spec::TuplePackOp, nbdl_spec::TupleExtractOp>(Op);
  }
};
} // namespace

void nbdl_spec::NbdlDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "nbdl_spec/NbdlTypes.cpp.inc"
    >();
  addAttributes<
#define GET_ATTRDEF_LIST
#include "nbdl_spec/NbdlAttrs.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "nbdl_spec/NbdlOps.cpp.inc"
      >();
  addInterfaces<NbdlInlinerInterface>();
}
#pragma clang diagnostic pop

nbdl_spec::StoreType
nbdl_spec::StoreType::get(mlir::MLIRContext* Ctx,
                          llvm::ArrayRef<mlir::TypeAttr> TypeAttrsRef) {
  llvm::SmallVector<mlir::TypeAttr, 8> TypeAttrs(TypeAttrsRef);
  // Sort and unique the inputs. (not stable)
  llvm::sort(TypeAttrs, [](auto const& A, auto const& B) {
      void const* AT = A.getValue().getTypeID().getAsOpaquePointer();
      void const* BT = B.getValue().getTypeID().getAsOpaquePointer();
      return AT < BT;
    });
  TypeAttrsRef = llvm::ArrayRef(TypeAttrs.begin(), llvm::unique(TypeAttrs));
  return Base::get(Ctx, TypeAttrsRef);
}

void nbdl_spec::ScopeOp::build(::mlir::OpBuilder&,
                              ::mlir::OperationState& odsState,
                              std::unique_ptr<::mlir::Region>&& body) {
  odsState.addRegion(std::move(body));
}

mlir::Value nbdl_spec::lookThroughLoopArgs(mlir::Value V) {
  while (auto Arg = llvm::dyn_cast<mlir::BlockArgument>(V)) {
    auto Loop = llvm::dyn_cast_or_null<LoopOp>(Arg.getOwner()->getParentOp());
    if (!Loop)
      break;
    V = Loop.getInits()[Arg.getArgNumber()];
  }
  return V;
}

nbdl_spec::FuncNameOp nbdl_spec::getFuncName(mlir::Value V) {
  V = lookThroughLoopArgs(V);
  if (auto Op = V.getDefiningOp<nbdl_spec::LiftStoreOp>())
    V = lookThroughLoopArgs(Op.getValue());
  return V.getDefiningOp<nbdl_spec::FuncNameOp>();
}

nbdl_spec::DefineStructOp nbdl_spec::lookupStruct(mlir::Operation* From,
                                                   StructType T) {
  auto M = From->getParentOfType<mlir::ModuleOp>();
  if (!M)
    return nullptr;
  return M.lookupSymbol<DefineStructOp>(T.getName());
}

mlir::Value nbdl_spec::DefineStructOp::lookupMember(llvm::StringRef Name) {
  if (isExternal())
    return nullptr;
  auto Cont = llvm::dyn_cast<ContOp>(getBody().front().getTerminator());
  if (!Cont)
    return nullptr;
  mlir::Value Store = Cont.getArg();
  while (auto SC = Store.getDefiningOp<StoreComposeOp>()) {
    mlir::Value Key = SC.getKey();
    if (auto MN = Key ? Key.getDefiningOp<MemberNameOp>() : MemberNameOp())
      if (MN.getName() == Name)
        return SC.getLhs();
    Store = SC.getRhs();
  }
  return nullptr;
}

llvm::SmallVector<std::pair<llvm::StringRef, mlir::Value>, 4>
nbdl_spec::DefineStructOp::getMembers() {
  llvm::SmallVector<std::pair<llvm::StringRef, mlir::Value>, 4> Members;
  if (isExternal())
    return Members;
  auto Cont = llvm::dyn_cast<ContOp>(getBody().front().getTerminator());
  if (!Cont)
    return Members;
  mlir::Value Store = Cont.getArg();
  while (auto SC = Store.getDefiningOp<StoreComposeOp>()) {
    mlir::Value Key = SC.getKey();
    if (auto MN = Key ? Key.getDefiningOp<MemberNameOp>() : MemberNameOp())
      Members.emplace_back(MN.getName(), SC.getLhs());
    Store = SC.getRhs();
  }
  // Order the members by definition.
  std::reverse(Members.begin(), Members.end());
  return Members;
}

// Get the value that a closure (possibly lifted to a store) is from.
static mlir::Value lookThroughClosureArg(mlir::Value V) {
  V = nbdl_spec::lookThroughLoopArgs(V);
  if (auto Op = V.getDefiningOp<nbdl_spec::LiftStoreOp>())
    V = nbdl_spec::lookThroughLoopArgs(Op.getValue());
  return V;
}

nbdl_spec::ClosureOp nbdl_spec::getClosure(mlir::Value V) {
  return lookThroughClosureArg(V).getDefiningOp<nbdl_spec::ClosureOp>();
}

nbdl_spec::ReturnClosureOp nbdl_spec::getReturnClosure(mlir::Value V) {
  return lookThroughClosureArg(V)
    .getDefiningOp<nbdl_spec::ReturnClosureOp>();
}

// Look up the function in the parent module of a visit
// if the callee is a function name.
static mlir::Operation* lookupCalleeSymbol(nbdl_spec::VisitOp Op) {
  nbdl_spec::FuncNameOp FN = nbdl_spec::getFuncName(Op.getFn());
  auto M = Op->getParentOfType<mlir::ModuleOp>();
  if (!FN || !M)
    return nullptr;
  return M.lookupSymbol(FN.getNameAttr());
}

mlir::func::FuncOp nbdl_spec::VisitOp::lookupCallee() {
  return llvm::dyn_cast_or_null<mlir::func::FuncOp>(lookupCalleeSymbol(*this));
}

nbdl_spec::DefineMatchFnOp nbdl_spec::VisitOp::lookupMatchFn() {
  return llvm::dyn_cast_or_null<DefineMatchFnOp>(lookupCalleeSymbol(*this));
}

// Print the symbol name and parameters like func.func.
//  nbdl.define_match_fn @name(%arg0: !nbdl.store, ...) attributes {...} {
//    ...
//  }
void nbdl_spec::DefineMatchFnOp::print(mlir::OpAsmPrinter& P) {
  P << ' ';
  P.printSymbolName(getSymName());
  P << '(';
  llvm::interleaveComma(getBody().getArguments(), P,
      [&](mlir::BlockArgument Arg) { P.printRegionArgument(Arg); });
  P << ')';
  P.printOptionalAttrDictWithKeyword((*this)->getAttrs(),
                                     {getSymNameAttrName()});
  P << ' ';
  P.printRegion(getBody(), /*printEntryBlockArgs=*/false);
}

mlir::ParseResult nbdl_spec::DefineMatchFnOp::parse(
    mlir::OpAsmParser& Parser, mlir::OperationState& Result) {
  mlir::StringAttr SymName;
  llvm::SmallVector<mlir::OpAsmParser::Argument, 4> Args;
  if (Parser.parseSymbolName(SymName) ||
      Parser.parseArgumentList(Args, mlir::OpAsmParser::Delimiter::Paren,
                               /*allowType=*/true) ||
      Parser.parseOptionalAttrDictWithKeyword(Result.attributes))
    return mlir::failure();
  Result.addAttribute(getSymNameAttrName(Result.name), SymName);
  return Parser.parseRegion(*Result.addRegion(), Args);
}

llvm::LogicalResult nbdl_spec::DefineMatchFnOp::verify() {
  if (getBody().empty())
    return emitOpError("expecting a body");
  for (mlir::BlockArgument Arg : getBody().getArguments())
    if (!llvm::isa<nbdl_spec::StoreType>(Arg.getType()))
      return emitOpError("parameter ") << Arg.getArgNumber()
        << " should be a store (" << Arg.getType() << ")";
  return llvm::success();
}

llvm::LogicalResult nbdl_spec::ClosureOp::verify() {
  for (mlir::BlockArgument Arg : getBody().getArguments())
    if (!llvm::isa<nbdl_spec::StoreType>(Arg.getType()))
      return emitOpError("parameter ") << Arg.getArgNumber()
        << " should be a store (" << Arg.getType() << ")";
  return llvm::success();
}

// A return may be nested in the regions of match operations
// until they are inlined by the flatten pass.
mlir::FunctionOpInterface nbdl_spec::ReturnOp::getReturnedFunction() {
  return (*this)->getParentOfType<mlir::FunctionOpInterface>();
}

llvm::LogicalResult nbdl_spec::LiftStoreOp::verify() {
  mlir::Type ValueT = getValue().getType();
  if (llvm::isa<nbdl_spec::StoreType>(ValueT))
    return emitOpError("cannot lift a value that is already a store");
  // A placeholder value lifts to an unresolved store.
  auto ExpectedT = schir_common::isPlaceholder(ValueT)
    ? nbdl_spec::StoreType::get(getContext())
    : nbdl_spec::StoreType::get(getContext(), mlir::TypeAttr::get(ValueT));
  if (getResult().getType() != ExpectedT)
    return emitOpError("result type (") << getResult().getType()
      << ") should be " << ExpectedT;
  return llvm::success();
}

// Return true if T is not yet inferred (ie a placeholder or
// a !nbdl.store with no resolved alternatives.)
static bool needsResolveT(mlir::Type T) {
  if (schir_common::isPlaceholder(T))
    return true;
  auto ST = llvm::dyn_cast<nbdl_spec::StoreType>(T);
  return ST && ST.getAlts().empty();
}

// Check that each value has the type of the corresponding
// block argument unless either is not yet inferred.
static llvm::LogicalResult verifyLoopCarriedTypes(mlir::Operation* Op,
                                                  mlir::ValueRange Values,
                                                  mlir::Block& Body) {
  if (Values.size() != Body.getNumArguments())
    return Op->emitOpError("expecting ") << Body.getNumArguments()
      << " loop-carried values but got " << Values.size();
  for (auto [I, V, Arg] : llvm::enumerate(Values, Body.getArguments())) {
    mlir::Type T = V.getType();
    if (T != Arg.getType() && !needsResolveT(T) &&
        !needsResolveT(Arg.getType()))
      return Op->emitOpError("type of loop-carried value ") << I << " ("
        << T << ") does not match the type of the loop argument ("
        << Arg.getType() << ")";
  }
  return llvm::success();
}

llvm::LogicalResult nbdl_spec::LoopOp::verify() {
  return verifyLoopCarriedTypes(*this, getInits(), getBody().front());
}

nbdl_spec::LoopOp nbdl_spec::ContinueOp::getLoop() {
  for (auto Loop = (*this)->getParentOfType<LoopOp>(); Loop;
       Loop = Loop->getParentOfType<LoopOp>())
    if (Loop.getFnAttr() == getFnAttr())
      return Loop;
  return {};
}

llvm::LogicalResult nbdl_spec::ContinueOp::verify() {
  LoopOp Loop = getLoop();
  if (!Loop)
    return emitOpError("expecting a parent nbdl.loop of ") << getFnAttr();
  return verifyLoopCarriedTypes(*this, getArgs(), Loop.getBody().front());
}

llvm::LogicalResult nbdl_spec::TuplePackOp::verify() {
  mlir::Type ResultT = getResult().getType();
  if (llvm::isa<StructType>(ResultT))
    return llvm::success();
  auto TupleT = llvm::dyn_cast<TupleType>(ResultT);
  if (!TupleT)
    return emitOpError("result should be a tuple or struct: ") << ResultT;
  if (!llvm::equal(TupleT.getTypes(), getElements().getTypes()))
    return emitOpError("element types should be the types of the result (")
      << ResultT << ")";
  return llvm::success();
}

llvm::LogicalResult nbdl_spec::TupleExtractOp::verify() {
  llvm::ArrayRef<mlir::Type> Ts =
    llvm::cast<TupleType>(getTuple().getType()).getTypes();
  uint64_t Index = getIndex();
  if (Index >= Ts.size())
    return emitOpError("index ") << Index << " is out of bounds";
  if (Ts[Index] != getResult().getType())
    return emitOpError("result type should be ") << Ts[Index];
  return llvm::success();
}

mlir::OpFoldResult nbdl_spec::TupleExtractOp::fold(FoldAdaptor) {
  // Get the element of a tuple_pack.
  if (auto Pack = getTuple().getDefiningOp<TuplePackOp>())
    return Pack.getElements()[getIndex()];
  return {};
}

std::string nbdl_spec::getCAdapterName(llvm::StringRef SymName) {
  // TODO Consider using schir::Mangle. (It requires schir::Context.)
  std::string Result = "nbdl_c_";
  llvm::SmallVector<llvm::StringRef, 4> Parts;
  SymName.split(Parts, "::", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (llvm::StringRef Part : Parts)
    Result += std::to_string(Part.size()) + Part.str();
  return Result;
}

// Get the type of a key as C++ decays it (ie without a top level const)
// where no key is the unit key or null if it is not yet inferred.
static mlir::Type getDecayedKeyType(mlir::MLIRContext* Ctx, mlir::Value Key) {
  if (!Key)
    return nbdl_spec::UnitType::get(Ctx);
  mlir::Type T = Key.getType();
  if (auto ST = llvm::dyn_cast<nbdl_spec::StoreType>(T);
      ST && ST.getAlts().size() == 1)
    T = ST.getAlts().front().getValue();
  if (needsResolveT(T))
    return {};
  if (auto CT = llvm::dyn_cast<nbdl_spec::CppType>(T)) {
    llvm::StringRef Typename = CT.getCppTypename();
    if (Typename.consume_front("const "))
      return nbdl_spec::CppType::get(Ctx, Typename);
  }
  return T;
}

mlir::Value nbdl_spec::lookThroughStoreCompose(mlir::Value Store,
                                               mlir::Value Key) {
  mlir::MLIRContext* Ctx = Store.getContext();
  mlir::Type KeyT = getDecayedKeyType(Ctx, Key);
  if (!KeyT)
    return {};
  while (auto Compose = Store.getDefiningOp<StoreComposeOp>()) {
    mlir::Type ComposeKeyT = getDecayedKeyType(Ctx, Compose.getKey());
    if (!ComposeKeyT)
      return {};
    if (ComposeKeyT == KeyT)
      break;
    Store = Compose.getRhs();
  }
  return Store;
}

// Get the store whose member a get with a key descends to.
mlir::Value nbdl_spec::GetOp::getParentStore(mlir::Value) {
  if (hasUnitKey())
    return {};
  return lookThroughStoreCompose(getState(), getKey());
}

// Get the store that a get with a unit key is a view of.
mlir::Value nbdl_spec::GetOp::getAliasedStore(mlir::Value) {
  if (!hasUnitKey())
    return {};
  return lookThroughStoreCompose(getState(), getKey());
}

mlir::Value nbdl_spec::ConstOp::getParentStore(mlir::Value) {
  return {};
}

// Alias the store since const is a view, not a child.
mlir::Value nbdl_spec::ConstOp::getAliasedStore(mlir::Value) {
  return getArg();
}

// Get the store whose element a keyed match descends to.
mlir::Value nbdl_spec::MatchOp::getParentStore(mlir::Value) {
  if (hasUnitKey())
    return {};
  return lookThroughStoreCompose(getStore(), getKey());
}

// Get the store that a match with a unit key is a view of.
mlir::Value nbdl_spec::MatchOp::getAliasedStore(mlir::Value) {
  if (!hasUnitKey())
    return {};
  return lookThroughStoreCompose(getStore(), getKey());
}

mlir::Value nbdl_spec::MatchEachOp::getParentStore(mlir::Value) {
  return getRange();
}

mlir::Value nbdl_spec::MatchEachOp::getAliasedStore(mlir::Value) {
  return {};
}

mlir::Value nbdl_spec::MatchIfOp::getParentStore(mlir::Value) {
  return {};
}

// Alias the cond unless it is raw (ie not a store.)
mlir::Value nbdl_spec::MatchIfOp::getAliasedStore(mlir::Value) {
  if (!llvm::isa<nbdl_spec::StoreType>(getCond().getType()))
    return {};
  return getCond();
}

// Defer until the argument is removed as invariant since a later
// continue may pass a different store.
mlir::Value nbdl_spec::LoopOp::getParentStore(mlir::Value) {
  return {};
}

mlir::Value nbdl_spec::LoopOp::getAliasedStore(mlir::Value) {
  return {};
}
