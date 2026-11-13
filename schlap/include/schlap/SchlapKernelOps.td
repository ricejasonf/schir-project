// SchlapKernelOps.td - Schlap term and proof operations
//
// Copyright Jason Rice 2026
//
// Define the operations of terms and proofs here.
//
#ifndef SCHLAP_KERNEL_OPS_TD
#define SCHLAP_KERNEL_OPS_TD

include "mlir/IR/SymbolInterfaces.td"

class Schlap_TernaryOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [Pure]> {
  let description = desc;
  let arguments = (ins AnyType:$first, AnyType:$second, AnyType:$third);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    operands attr-dict `:` functional-type(operands, results)
  }];
}

// Top-level declarations

class Schlap_DeclOp<string mnemonic, list<Trait> traits = []>
    : Schlap_Op<mnemonic, !listconcat([Symbol, IsolatedFromAbove], traits)> {
  let hasRegionVerifier = 1;
}

def Schlap_TheoremOp : Schlap_DeclOp<"theorem"> {
  let description = [{
    Declare a theorem with its statement and its proof.

    Mark `checked` and list the `axioms` the proof depends on once the
    proof checks.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name, UnitAttr:$checked,
                       OptionalAttr<ArrayAttr>:$axioms);
  let regions = (region SizedRegion<1>:$statement, SizedRegion<1>:$proof);
  let assemblyFormat = [{
    $sym_name attr-dict-with-keyword $statement `proof` $proof
  }];
}

def Schlap_DefinitionOp : Schlap_DeclOp<"definition"> {
  let description = [{
    Declare a name for a term of a type.

    Mark `checked` and list the `axioms` the term depends on once the term
    checks.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name, UnitAttr:$checked,
                       OptionalAttr<ArrayAttr>:$axioms);
  let regions = (region SizedRegion<1>:$signature, SizedRegion<1>:$body);
  let assemblyFormat = [{
    $sym_name attr-dict-with-keyword `:` $signature `=` $body
  }];
}

def Schlap_AxiomOp : Schlap_DeclOp<"axiom"> {
  let description = [{
    Declare a statement that is assumed without proof.

    Mark `checked` once the statement is known to be a proposition.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name, UnitAttr:$checked);
  let regions = (region SizedRegion<1>:$statement);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $statement";
}

def Schlap_RefOp : Schlap_Op<"ref", [Pure]> {
  let description = [{
    Refer to a theorem, definition or axiom declared earlier.
  }];
  let arguments = (ins FlatSymbolRefAttr:$name);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$name attr-dict `:` type($result)";
}

def Schlap_HoldOp : Schlap_Op<"hold", [SingleBlock, NoTerminator]> {
  let description = [{
    Hold the terms computed while checking a declaration.
  }];
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}

// Sorts and functions

def Schlap_PropSortOp : Schlap_NullaryOp<"prop_sort", [{
  Create the sort of propositions.
}]>;

def Schlap_TypeSortOp : Schlap_NullaryOp<"type_sort", [{
  Create the sort of types, which has no type.
}]>;

def Schlap_PiOp : Schlap_BinderOp<"pi", [{
  Create the type of functions whose result type is the body.
}]>;

def Schlap_LambdaOp
    : Schlap_Op<"lambda", [RecursiveMemoryEffects, SingleBlock]> {
  let description = [{
    Create a function of the bound variable whose type comes from the
    expected type.
  }];
  let results = (outs AnyType:$result);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$body attr-dict-with-keyword `:` type($result)";
  let hasRegionVerifier = 1;
}

def Schlap_AppOp : Schlap_BinaryOp<"app", [{
  Apply a function to an argument.
}]>;

// Propositions

def Schlap_TopIntroOp : Schlap_NullaryOp<"top_intro", [{
  Prove ⊤.
}]>;

def Schlap_FalseElimOp : Schlap_UnaryOp<"false_elim", [{
  Prove the expected type from a proof of ⊥.
}]>;

def Schlap_AndIntroOp : Schlap_BinaryOp<"and_intro", [{
  Prove a conjunction from a proof of each side.
}]>;

def Schlap_AndLeftOp : Schlap_UnaryOp<"and_left", [{
  Prove the left side of a conjunction.
}]>;

def Schlap_AndRightOp : Schlap_UnaryOp<"and_right", [{
  Prove the right side of a conjunction.
}]>;

def Schlap_OrInlOp : Schlap_UnaryOp<"or_inl", [{
  Prove a disjunction from a proof of its left side.
}]>;

def Schlap_OrInrOp : Schlap_UnaryOp<"or_inr", [{
  Prove a disjunction from a proof of its right side.
}]>;

def Schlap_OrElimOp : Schlap_TernaryOp<"or_elim", [{
  Prove the expected proposition from a disjunction and a function for
  each side.
}]>;

def Schlap_ExistsIntroOp : Schlap_BinaryOp<"exists_intro", [{
  Prove an existential from a witness and a proof for it.
}]>;

def Schlap_ExistsElimOp : Schlap_BinaryOp<"exists_elim", [{
  Prove the expected proposition from an existential and a function of
  the witness and its proof.
}]>;

def Schlap_ReflOp : Schlap_NullaryOp<"refl", [{
  Prove an equality whose sides compute to the same term.
}]>;

def Schlap_SubstOp : Schlap_TernaryOp<"subst", [{
  Prove the motive of the right side of an equality from the motive of
  its left side.
}]>;

// Natural numbers

def Schlap_NatOp : Schlap_NullaryOp<"nat", [{
  Create the type of natural numbers.
}]>;

def Schlap_ZeroOp : Schlap_NullaryOp<"zero", [{
  Create the natural number zero.
}]>;

def Schlap_SuccOp : Schlap_UnaryOp<"succ", [{
  Create the successor of a natural number.
}]>;

def Schlap_NatRecOp : Schlap_Op<"nat_rec", [Pure]> {
  let description = [{
    Recurse on a natural number with a motive, a base case and a step.
  }];
  let arguments = (ins AnyType:$motive, AnyType:$base, AnyType:$step,
                       AnyType:$n);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    operands attr-dict `:` functional-type(operands, results)
  }];
}

#endif  // SCHLAP_KERNEL_OPS_TD
