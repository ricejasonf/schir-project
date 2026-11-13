// Schlap.td - MLIR Dialect Definitions for Schlap
//
// Copyright Jason Rice 2026
//
#ifndef SCHLAP_DIALECT_TD
#define SCHLAP_DIALECT_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def Schlap_Dialect : Dialect {
  let name = "schlap";
  let cppNamespace = "::schlap";
  let useDefaultTypePrinterParser = 1;
  let hasConstantMaterializer = 1;
}

// Types

class Schlap_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Schlap_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

def Schlap_Unknown : Schlap_Type<"Unknown", "unknown"> {
  let description = [{
    Mark a value whose type is not inferred yet.
  }];
}

def Schlap_Prop : Schlap_Type<"Prop", "prop"> {
  let description = [{
    Type a formula.
  }];
}

def Schlap_Int : Schlap_Type<"Int", "int"> {
  let description = [{
    Type a 64-bit integer.
  }];
}

def Schlap_Set : Schlap_Type<"Set", "set"> {
  let description = [{
    Type a finite set of elements of one type.
  }];
  let parameters = (ins "mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def Schlap_Atom : Schlap_Type<"Atom", "atom"> {
  let description = [{
    Type a named model value.
  }];
}

def Schlap_Tuple : Schlap_Type<"Tuple", "tuple"> {
  let description = [{
    Type a tuple of values of the given types.
  }];
  let parameters = (ins ArrayRefParameter<"mlir::Type">:$elementTypes);
  let assemblyFormat = "`<` $elementTypes `>`";
}

def Schlap_Fn : Schlap_Type<"Fn", "fn"> {
  let description = [{
    Type a function from a finite domain.
  }];
  let parameters = (ins "mlir::Type":$domainType, "mlir::Type":$rangeType);
  let assemblyFormat = "`<` $domainType `,` $rangeType `>`";
}

def Schlap_Nat : Schlap_Type<"Nat", "nat"> {
  let description = [{
    Type a natural number term.
  }];
}

def Schlap_Proof : Schlap_Type<"Proof", "proof"> {
  let description = [{
    Type a proof term.
  }];
}

def Schlap_Term : Schlap_Type<"Term", "term"> {
  let description = [{
    Type a term that is not a proposition, a proof or a natural number.
  }];
}

// Add further types above this line.

// Operations

class Schlap_Op<string mnemonic, list<Trait> traits = []> :
    Op<Schlap_Dialect, mnemonic, traits>;

class Schlap_NullaryOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [Pure]> {
  let description = desc;
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` type($result)";
}

class Schlap_UnaryOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [Pure]> {
  let description = desc;
  let arguments = (ins AnyType:$arg);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $arg attr-dict `:` custom<OpTypes>(type($arg), type($result))
  }];
}

class Schlap_BinaryOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [Pure]> {
  let description = desc;
  let arguments = (ins AnyType:$lhs, AnyType:$rhs);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $lhs `,` $rhs attr-dict `:`
      custom<OpTypes>(type($lhs), type($rhs), type($result))
  }];
}

class Schlap_BinderOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [RecursiveMemoryEffects, SingleBlock]> {
  let description = desc;
  let arguments = (ins AnyType:$domain);
  let results = (outs AnyType:$result);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = [{
    custom<Binder>($body, $domain, type($domain), type($result))
      attr-dict-with-keyword
  }];
  let hasRegionVerifier = 1;
}

// Formulas

def Schlap_AndOp : Schlap_BinaryOp<"and", [{
  Take the conjunction of two formulas.
}]> {
  let hasFolder = 1;
}

def Schlap_OrOp : Schlap_BinaryOp<"or", [{
  Take the disjunction of two formulas.
}]> {
  let hasFolder = 1;
}

def Schlap_NotOp : Schlap_UnaryOp<"not", [{
  Take the negation of a formula.
}]> {
  let hasFolder = 1;
}

def Schlap_ImpliesOp : Schlap_BinaryOp<"implies", [{
  State that the first formula implies the second.
}]> {
  let hasFolder = 1;
}

def Schlap_IffOp : Schlap_BinaryOp<"iff", [{
  State that two formulas imply each other.
}]> {
  let hasFolder = 1;
}

def Schlap_EqOp : Schlap_BinaryOp<"eq", [{
  State that two values are equal.
}]> {
  let hasFolder = 1;
}

def Schlap_NeOp : Schlap_BinaryOp<"ne", [{
  State that two values are not equal.
}]> {
  let hasFolder = 1;
}

def Schlap_TrueOp : Schlap_NullaryOp<"true", [{
  Create the formula that always holds.
}]> {
  let traits = [Pure, ConstantLike];
  let hasFolder = 1;
}

def Schlap_FalseOp : Schlap_NullaryOp<"false", [{
  Create the formula that never holds.
}]> {
  let traits = [Pure, ConstantLike];
  let hasFolder = 1;
}

def Schlap_ForallOp : Schlap_BinderOp<"forall", [{
  State that the body holds for every value of the bound variable.
}]>;

def Schlap_ExistsOp : Schlap_BinderOp<"exists", [{
  State that the body holds for some value of the bound variable.
}]>;

def Schlap_YieldOp : Schlap_Op<"yield", [Pure, Terminator]> {
  let description = [{
    Yield the value of a region.
  }];
  let arguments = (ins AnyType:$value);
  let assemblyFormat = "$value attr-dict `:` type($value)";
}

// Integers

def Schlap_IntOp : Schlap_Op<"int", [Pure, ConstantLike]> {
  let description = [{
    Create an integer constant.
  }];
  let arguments = (ins I64Attr:$value);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
  let hasFolder = 1;
}

def Schlap_AddOp : Schlap_BinaryOp<"add", [{
  Add two integers.
}]> {
  let hasFolder = 1;
}

def Schlap_SubOp : Schlap_BinaryOp<"sub", [{
  Subtract the second integer from the first.
}]> {
  let hasFolder = 1;
}

def Schlap_MulOp : Schlap_BinaryOp<"mul", [{
  Multiply two integers.
}]> {
  let hasFolder = 1;
}

def Schlap_NegOp : Schlap_UnaryOp<"neg", [{
  Negate an integer.
}]> {
  let hasFolder = 1;
}

def Schlap_LtOp : Schlap_BinaryOp<"lt", [{
  State that the first integer is less than the second.
}]> {
  let hasFolder = 1;
}

def Schlap_LeOp : Schlap_BinaryOp<"le", [{
  State that the first integer is at most the second.
}]> {
  let hasFolder = 1;
}

def Schlap_GtOp : Schlap_BinaryOp<"gt", [{
  State that the first integer is greater than the second.
}]> {
  let hasFolder = 1;
}

def Schlap_GeOp : Schlap_BinaryOp<"ge", [{
  State that the first integer is at least the second.
}]> {
  let hasFolder = 1;
}

// Sets

def Schlap_MemberOp : Schlap_BinaryOp<"member", [{
  State that a value is an element of a set.
}]> {
  let hasFolder = 1;
}

def Schlap_NotMemberOp : Schlap_BinaryOp<"not_member", [{
  State that a value is not an element of a set.
}]> {
  let hasFolder = 1;
}

def Schlap_UnionOp : Schlap_BinaryOp<"union", [{
  Take the union of two sets.
}]> {
  let hasFolder = 1;
}

def Schlap_IntersectOp : Schlap_BinaryOp<"intersect", [{
  Take the intersection of two sets.
}]> {
  let hasFolder = 1;
}

def Schlap_SubsetOp : Schlap_BinaryOp<"subset", [{
  State that every element of the first set is in the second.
}]> {
  let hasFolder = 1;
}

def Schlap_RangeOp : Schlap_BinaryOp<"range", [{
  Create the set of integers from the first to the second inclusive.
}]> {
  let hasFolder = 1;
}

def Schlap_EmptySetOp : Schlap_NullaryOp<"empty_set", [{
  Create the empty set.
}]> {
  let traits = [Pure, ConstantLike];
  let hasFolder = 1;
}

include "schlap/SchlapSpecOps.td"
include "schlap/SchlapKernelOps.td"

#endif  // SCHLAP_DIALECT_TD
