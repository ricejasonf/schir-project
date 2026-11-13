#include <schlap/Dialect.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/ImplicitLocOpBuilder.h>
#include <mlir/IR/DialectImplementation.h>
#include <mlir/IR/OpImplementation.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/TypeSwitch.h>
#include <array>

// Parse the operand and result types as one type or as a function type.
template <typename... Types>
static mlir::ParseResult parseOpTypes(mlir::OpAsmParser& Parser,
                                      Types&... Ts) {
  std::array<mlir::Type*, sizeof...(Ts)> Refs{&Ts...};
  mlir::Type Type;
  if (Parser.parseType(Type))
    return mlir::failure();

  auto FT = dyn_cast<mlir::FunctionType>(Type);
  if (FT && FT.getNumInputs() + 1 == Refs.size() && FT.getNumResults() == 1) {
    for (auto [Ref, Input] : llvm::zip(Refs, FT.getInputs()))
      *Ref = Input;
    *Refs.back() = FT.getResult(0);
    return mlir::success();
  }

  for (mlir::Type* Ref : Refs)
    *Ref = Type;
  return mlir::success();
}

// Print the operand and result types as one type when they are all equal.
template <typename... Types>
static void printOpTypes(mlir::OpAsmPrinter& Printer, mlir::Operation* Op,
                         Types... Ts) {
  std::array<mlir::Type, sizeof...(Ts)> All{Ts...};
  if (llvm::all_equal(All)) {
    Printer << All.front();
    return;
  }
  Printer << mlir::FunctionType::get(Op->getContext(),
                                     llvm::ArrayRef(All).drop_back(),
                                     All.back());
}

// Parse `(%x : T) in %domain : DomainType -> ResultType { body }`.
static mlir::ParseResult
parseBinder(mlir::OpAsmParser& Parser, mlir::Region& Body,
            mlir::OpAsmParser::UnresolvedOperand& Domain,
            mlir::Type& DomainType, mlir::Type& ResultType) {
  mlir::OpAsmParser::Argument Var;
  if (Parser.parseLParen() ||
      Parser.parseArgument(Var, /*allowType=*/true) ||
      Parser.parseRParen() ||
      Parser.parseKeyword("in") ||
      Parser.parseOperand(Domain) ||
      Parser.parseColonType(DomainType) ||
      Parser.parseArrow() ||
      Parser.parseType(ResultType))
    return mlir::failure();
  return Parser.parseRegion(Body, Var);
}

// Print `(%x : T) in %domain : DomainType -> ResultType { body }`.
static void printBinder(mlir::OpAsmPrinter& Printer, mlir::Operation*,
                        mlir::Region& Body, mlir::Value Domain,
                        mlir::Type DomainType, mlir::Type ResultType) {
  Printer << '(';
  Printer.printRegionArgument(Body.getArgument(0));
  Printer << ") in " << Domain << " : " << DomainType << " -> "
          << ResultType << ' ';
  Printer.printRegion(Body, /*printEntryBlockArgs=*/false);
}

// Verify that a binder body has one bound variable and yields a value.
static llvm::LogicalResult verifyBinder(mlir::Operation* Op) {
  mlir::Block& Block = Op->getRegion(0).front();
  if (Block.getNumArguments() != 1)
    return Op->emitOpError("expects one bound variable");
  if (Block.empty() || !isa<schlap::YieldOp>(Block.back()))
    return Op->emitOpError("expects the body to end with schlap.yield");
  return mlir::success();
}

// Include generated source files (from the build dir.)
#include "schlap/SchlapDialect.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "schlap/SchlapTypes.cpp.inc"

#define GET_OP_CLASSES
#include "schlap/SchlapOps.cpp.inc"

void schlap::SchlapDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "schlap/SchlapTypes.cpp.inc"
    >();

  addOperations<
#define GET_OP_LIST
#include "schlap/SchlapOps.cpp.inc"
      >();
}

llvm::LogicalResult schlap::ForallOp::verifyRegions() {
  return verifyBinder(*this);
}

llvm::LogicalResult schlap::ExistsOp::verifyRegions() {
  return verifyBinder(*this);
}

llvm::LogicalResult schlap::FnOp::verifyRegions() {
  return verifyBinder(*this);
}

llvm::LogicalResult schlap::PiOp::verifyRegions() {
  return verifyBinder(*this);
}
