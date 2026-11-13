#ifndef SCHLAP_VALUE_H
#define SCHLAP_VALUE_H

#include <mlir/IR/Attributes.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Types.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdint>
#include <optional>

// Represent the values of specifications as attributes.
//
// An integer is an i64 IntegerAttr, a truth value a BoolAttr, an atom a
// StringAttr, a tuple an ArrayAttr of its elements, a set an ArrayAttr of
// its elements in the order of compareValues, and a function an ArrayAttr
// of [argument, value] pairs in the order of their arguments.
namespace schlap {
// Return the integer of an integer value.
std::optional<int64_t> getIntValue(mlir::Attribute Value);

// Create an integer value.
mlir::Attribute getIntValue(mlir::MLIRContext* Context, int64_t N);

// Order values totally and the same way in every run.
int compareValues(mlir::Attribute A, mlir::Attribute B);

// Create the set of the elements in any order.
mlir::ArrayAttr getSetValue(mlir::MLIRContext* Context,
                            llvm::SmallVectorImpl<mlir::Attribute>& Elements);

// Return the element of a set equal to Element or null.
mlir::Attribute findInSet(mlir::ArrayAttr Set, mlir::Attribute Element);

// Return the pair of a function whose argument is Arg or null.
mlir::ArrayAttr findInFunction(mlir::ArrayAttr Fn, mlir::Attribute Arg);

// Print a value of the given type.
void printValue(llvm::raw_ostream& OS, mlir::Attribute Value, mlir::Type Type);
}  // namespace schlap

#endif  // SCHLAP_VALUE_H
