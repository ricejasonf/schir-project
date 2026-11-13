#include <schlap/Dialect.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/OpImplementation.h>
#include <mlir/IR/SymbolTable.h>

// Return true if V is a state variable.
static bool isVariable(mlir::Value V) {
  return V.getDefiningOp<schlap::VariableOp>() != nullptr;
}

llvm::LogicalResult schlap::SpecOp::verifyRegions() {
  unsigned NumInit = 0, NumNext = 0;
  for (mlir::Operation& Op : getBody().front()) {
    if (isa<InitOp>(Op))
      ++NumInit;
    else if (isa<NextOp>(Op))
      ++NumNext;
  }
  if (NumInit != 1)
    return emitOpError("expects exactly one schlap.init");
  if (NumNext != 1)
    return emitOpError("expects exactly one schlap.next");
  return mlir::success();
}

llvm::LogicalResult schlap::PrimeOp::verify() {
  if (!isVariable(getVar()))
    return emitOpError("expects a state variable");
  return mlir::success();
}

llvm::LogicalResult schlap::UnchangedOp::verify() {
  if (!llvm::all_of(getVars(), isVariable))
    return emitOpError("expects state variables");
  return mlir::success();
}

llvm::LogicalResult schlap::AssignOp::verify() {
  if (!isVariable(getVar()))
    return emitOpError("expects a state variable");
  return mlir::success();
}

// Verify that Op refers to an action of its specification.
static llvm::LogicalResult
verifyActionSymbol(mlir::Operation* Op, mlir::FlatSymbolRefAttr Action,
                   mlir::SymbolTableCollection& SymbolTable) {
  if (!SymbolTable.lookupNearestSymbolFrom<schlap::ActionOp>(Op, Action))
    return Op->emitOpError("expects an action: ") << Action;
  return mlir::success();
}

llvm::LogicalResult schlap::ActionRefOp::verifySymbolUses(
    mlir::SymbolTableCollection& SymbolTable) {
  return verifyActionSymbol(*this, getActionAttr(), SymbolTable);
}

llvm::LogicalResult schlap::InActionOp::verifySymbolUses(
    mlir::SymbolTableCollection& SymbolTable) {
  return verifyActionSymbol(*this, getActionAttr(), SymbolTable);
}

// Parse `(%x : T) in %domain : DomainType { body }`.
mlir::ParseResult schlap::ChooseOp::parse(mlir::OpAsmParser& Parser,
                                          mlir::OperationState& Result) {
  mlir::OpAsmParser::Argument Var;
  mlir::OpAsmParser::UnresolvedOperand Domain;
  mlir::Type DomainType;
  mlir::Region* Body = Result.addRegion();
  if (Parser.parseLParen() ||
      Parser.parseArgument(Var, /*allowType=*/true) ||
      Parser.parseRParen() ||
      Parser.parseKeyword("in") ||
      Parser.parseOperand(Domain) ||
      Parser.parseColonType(DomainType) ||
      Parser.resolveOperand(Domain, DomainType, Result.operands) ||
      Parser.parseRegion(*Body, Var) ||
      Parser.parseOptionalAttrDict(Result.attributes))
    return mlir::failure();
  ChooseOp::ensureTerminator(*Body, Parser.getBuilder(), Result.location);
  return mlir::success();
}

// Print `(%x : T) in %domain : DomainType { body }`.
void schlap::ChooseOp::print(mlir::OpAsmPrinter& Printer) {
  mlir::Region& Body = getBody();
  Printer << " (";
  Printer.printRegionArgument(Body.getArgument(0));
  Printer << ") in " << getDomain() << " : " << getDomain().getType() << ' ';
  Printer.printRegion(Body, /*printEntryBlockArgs=*/false,
                      /*printBlockTerminators=*/false);
  Printer.printOptionalAttrDict((*this)->getAttrs());
}

llvm::LogicalResult schlap::ChooseOp::verifyRegions() {
  if (getBody().getNumArguments() != 1)
    return emitOpError("expects one bound variable");
  return mlir::success();
}

// Verify that an event has one argument and ends in yield or done.
static llvm::LogicalResult verifyEvent(mlir::Operation* Op) {
  mlir::Block& Body = Op->getRegion(0).front();
  if (Body.getNumArguments() != 1)
    return Op->emitOpError("expects one block argument");
  if (Body.empty() || !isa<schlap::YieldOp, schlap::DoneOp>(Body.back()))
    return Op->emitOpError("expects the body to end with schlap.yield or "
                           "schlap.done");
  return mlir::success();
}

llvm::LogicalResult schlap::OnMutateOp::verifyRegions() {
  return verifyEvent(*this);
}

llvm::LogicalResult schlap::OnUseOp::verifyRegions() {
  return verifyEvent(*this);
}
