// SchlapSpecOps.td - Schlap specification operations
//
// Copyright Jason Rice 2026
//
// Define the operations of specifications and rules here.
//
#ifndef SCHLAP_SPEC_OPS_TD
#define SCHLAP_SPEC_OPS_TD

include "mlir/IR/SymbolInterfaces.td"
include "mlir/IR/RegionKindInterface.td"

// Specifications

def Schlap_SpecOp : Schlap_Op<"spec", [IsolatedFromAbove, Symbol, SymbolTable,
                                      NoTerminator, SingleBlock]> {
  let description = [{
    Define a specification from its variables, its initial condition, its
    next-state relation and its invariants.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
  let hasRegionVerifier = 1;
}

def Schlap_VariableOp : Schlap_Op<"variable", [Pure,
    ParentOneOf<["SpecOp", "RuleOp"]>]> {
  let description = [{
    Declare a state variable of a specification or a rule.
  }];
  let arguments = (ins StrAttr:$name);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$name attr-dict `:` type($result)";
}

class Schlap_FormulaRegionOp<string mnemonic, string desc,
                             list<Trait> traits = [],
                             Trait parent = HasParent<"SpecOp">>
    : Schlap_Op<mnemonic, !listconcat(traits, [parent, SingleBlock])> {
  let description = desc;
  let regions = (region SizedRegion<1>:$body);
}

def Schlap_InitOp : Schlap_FormulaRegionOp<"init", [{
  Hold the initial condition of a specification or a rule.
}], [], ParentOneOf<["SpecOp", "RuleOp"]>> {
  let assemblyFormat = "attr-dict-with-keyword $body";
}

def Schlap_ActionOp : Schlap_FormulaRegionOp<"action", [{
  Name a formula over the current and the next state.
}], [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

def Schlap_NextOp : Schlap_FormulaRegionOp<"next", [{
  Hold the next-state relation of a specification.
}]> {
  let assemblyFormat = "attr-dict-with-keyword $body";
}

def Schlap_InvariantOp : Schlap_FormulaRegionOp<"invariant", [{
  Name a formula that holds in every reachable state.
}], [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

def Schlap_ActionRefOp : Schlap_Op<"action_ref", [Pure,
    DeclareOpInterfaceMethods<SymbolUserOpInterface>]> {
  let description = [{
    Refer to the formula of an action.
  }];
  let arguments = (ins FlatSymbolRefAttr:$action);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$action attr-dict `:` type($result)";
}

def Schlap_PrimeOp : Schlap_Op<"prime", [Pure]> {
  let description = [{
    Take the value of a state variable in the next state.
  }];
  let arguments = (ins AnyType:$var);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $var attr-dict `:` custom<OpTypes>(type($var), type($result))
  }];
  let hasVerifier = 1;
}

def Schlap_UnchangedOp : Schlap_Op<"unchanged", [Pure]> {
  let description = [{
    State that each variable has the same value in the next state.
  }];
  let arguments = (ins Variadic<AnyType>:$vars);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $vars attr-dict `:` functional-type($vars, $result)
  }];
  let hasVerifier = 1;
}

// Rules

def Schlap_RuleOp : Schlap_Op<"rule", [IsolatedFromAbove, Symbol,
                                      NoTerminator, SingleBlock]> {
  let description = [{
    Define a rule about the IR of a function as a specification with a
    hole: its variables and initial condition, and what happens at each
    kind of event (schlap.on_mutate, schlap.on_use). The pass
    schlap-apply-rule fills the hole with the operations of a function.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

class Schlap_EventOp<string mnemonic, string desc>
    : Schlap_Op<mnemonic, [HasParent<"RuleOp">, SingleBlock]> {
  let description = desc;
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
  let hasRegionVerifier = 1;
}

def Schlap_OnMutateOp : Schlap_EventOp<"on_mutate", [{
  State what happens when an operation mutates a value, which is the
  argument of the block. The body holds schlap.require operations and ends
  in a schlap.yield of a formula over the current and the next state, or
  in a schlap.done if the state does not change.
}]>;

def Schlap_OnUseOp : Schlap_EventOp<"on_use", [{
  State what happens when an operation uses a value, which is the argument
  of the block. The body is as for schlap.on_mutate.
}]>;

def Schlap_RequireOp : Schlap_Op<"require", [
    ParentOneOf<["OnMutateOp", "OnUseOp"]>]> {
  let description = [{
    Require a formula to hold before the event happens.
  }];
  let arguments = (ins AnyType:$cond);
  let assemblyFormat = "$cond attr-dict `:` type($cond)";
}

def Schlap_RootOp : Schlap_Op<"root", [Pure]> {
  let description = [{
    Take the value of the function that a value is read from, followed back
    until a value that is not read from another.
  }];
  let arguments = (ins AnyType:$value);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $value attr-dict `:` custom<OpTypes>(type($value), type($result))
  }];
}

def Schlap_ReadFromOp : Schlap_Op<"read_from", [Pure]> {
  let description = [{
    Take the set of the values of the function that are read from a value,
    directly or through others, and are defined before the event.
  }];
  let arguments = (ins AnyType:$value);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $value attr-dict `:` custom<OpTypes>(type($value), type($result))
  }];
}

def Schlap_OwnedByOp : Schlap_Op<"owned_by", [Pure]> {
  let description = [{
    Take the set of the values of the function that a value may own, as a
    part of its object, and are defined before the event.
  }];
  let arguments = (ins AnyType:$value);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $value attr-dict `:` custom<OpTypes>(type($value), type($result))
  }];
}

// Values

def Schlap_IteOp : Schlap_Op<"ite", [Pure]> {
  let description = [{
    Choose the second operand if the first holds and the third otherwise.
  }];
  let arguments = (ins AnyType:$cond, AnyType:$lhs, AnyType:$rhs);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $cond `,` $lhs `,` $rhs attr-dict `:`
      custom<OpTypes>(type($cond), type($lhs), type($rhs), type($result))
  }];
  let hasFolder = 1;
}

def Schlap_AtomOp : Schlap_Op<"atom", [Pure, ConstantLike]> {
  let description = [{
    Create a named model value.
  }];
  let arguments = (ins StrAttr:$name);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$name attr-dict `:` type($result)";
  let hasFolder = 1;
}

def Schlap_SetOfOp : Schlap_Op<"set_of", [Pure]> {
  let description = [{
    Create the set of the operands.
  }];
  let arguments = (ins Variadic<AnyType>:$elements);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    ($elements^)? attr-dict `:` functional-type($elements, $result)
  }];
  let hasFolder = 1;
}

def Schlap_TupleOp : Schlap_Op<"tuple", [Pure]> {
  let description = [{
    Create a tuple of the operands.
  }];
  let arguments = (ins Variadic<AnyType>:$elements);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    ($elements^)? attr-dict `:` functional-type($elements, $result)
  }];
  let hasFolder = 1;
}

def Schlap_TupleGetOp : Schlap_Op<"tuple_get", [Pure]> {
  let description = [{
    Take the element of a tuple at a zero-based index.
  }];
  let arguments = (ins AnyType:$tuple, I64Attr:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $tuple `[` $index `]` attr-dict `:`
      custom<OpTypes>(type($tuple), type($result))
  }];
  let hasFolder = 1;
}

def Schlap_FnOp : Schlap_BinderOp<"fn", [{
  Create the function that maps each element of the domain to the body.
}]>;

def Schlap_ApplyOp : Schlap_BinaryOp<"apply", [{
  Apply a function to an element of its domain.
}]> {
  let hasFolder = 1;
}

def Schlap_UpdateOp : Schlap_Op<"update", [Pure]> {
  let description = [{
    Create the function that maps the second operand to the third and is
    otherwise the first.
  }];
  let arguments = (ins AnyType:$fn, AnyType:$arg, AnyType:$value);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $fn `,` $arg `,` $value attr-dict `:`
      custom<OpTypes>(type($fn), type($arg), type($value), type($result))
  }];
  let hasFolder = 1;
}

// Executable next-state relations

def Schlap_DoneOp : Schlap_Op<"done", [Pure, Terminator]> {
  let description = [{
    End a sequence of statements.
  }];
  let assemblyFormat = "attr-dict";
}

def Schlap_GuardOp : Schlap_Op<"guard", []> {
  let description = [{
    Continue only if the formula holds.
  }];
  let arguments = (ins AnyType:$cond);
  let assemblyFormat = "$cond attr-dict `:` type($cond)";
}

def Schlap_AssignOp : Schlap_Op<"assign", []> {
  let description = [{
    Set the next value of a variable, or continue only if it is equal when
    it is already set.
  }];
  let arguments = (ins AnyType:$var, AnyType:$value);
  let assemblyFormat = [{
    $var `,` $value attr-dict `:` custom<OpTypes>(type($var), type($value))
  }];
  let hasVerifier = 1;
}

def Schlap_EitherOp : Schlap_Op<"either", [
    SingleBlockImplicitTerminator<"DoneOp">, RecursiveMemoryEffects]> {
  let description = [{
    Continue with each region in turn.
  }];
  let regions = (region VariadicRegion<SizedRegion<1>>:$branches);
  let assemblyFormat = "$branches attr-dict";
}

def Schlap_ChooseOp : Schlap_Op<"choose", [
    SingleBlockImplicitTerminator<"DoneOp">, RecursiveMemoryEffects]> {
  let description = [{
    Continue with the region once for each element of the domain.
  }];
  let arguments = (ins AnyType:$domain);
  let regions = (region SizedRegion<1>:$body);
  let hasCustomAssemblyFormat = 1;
  let hasRegionVerifier = 1;
}

def Schlap_InActionOp : Schlap_Op<"in_action", [
    SingleBlockImplicitTerminator<"DoneOp">, RecursiveMemoryEffects,
    DeclareOpInterfaceMethods<SymbolUserOpInterface>]> {
  let description = [{
    Continue with the region as a step of the action.
  }];
  let arguments = (ins FlatSymbolRefAttr:$action);
  let regions = (region SizedRegion<1>:$body);
  let assemblyFormat = "$action attr-dict-with-keyword $body";
}

#endif  // SCHLAP_SPEC_OPS_TD
