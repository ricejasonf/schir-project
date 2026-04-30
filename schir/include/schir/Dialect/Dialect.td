//===- Ops.td - MLIR Dialect Definitions for SchirScheme ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Defines operations for SchirScheme dialect of MLIR
//
//===----------------------------------------------------------------------===//
#ifndef SCHIR_DIALECT_TD
#define SCHIR_DIALECT_TD

include "mlir/IR/OpBase.td"
include "mlir/IR/RegionKindInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def Schir_Dialect : Dialect {
  let name = "schir";
  let useDefaultTypePrinterParser = 1;
  let extraClassDeclaration = [{
    mlir::Attribute parseAttribute(mlir::DialectAsmParser& P,
                                   mlir::Type type) const override;
    void printAttribute(mlir::Attribute Attr,
                        mlir::DialectAsmPrinter& P) const override;
  }];
}
include "schir/Dialect/Types.td"

def Schir_ValueAttr : AttrDef<Schir_Dialect, "SchirValue"> {
  let attrName = "schir.value_attr";
  let parameters = (ins "StringAttr":$expr);
  let genStorageClass = 0;
  let extraClassDeclaration = [{
    static SchirValueAttr get(mlir::MLIRContext*, schir::Value Val);
    // getCachedValue - For garbage collector access.
    schir::Value& getCachedValue();
    schir::Value getValue(schir::Context& C) const;
  }];
}

// All valid run-time values (ie not Syntax)
def Schir_Type : AnyTypeOf<[
  Schir_Empty,
  Schir_Pair,
  Schir_Procedure,
  Schir_Undefined,
  Schir_Value,
  Schir_Vector,
  ]>;

def Schir_ValueOrBinding : AnyTypeOf<[
  Schir_Binding,
  Schir_Type,
  Schir_Unknown,
  ]>;

class SchirOp<string mnemonic, list<Trait> traits = []> :
    Op<Schir_Dialect, mnemonic, traits>;

def Schir_ApplyOp : SchirOp<"apply", [Terminator]> {
  let summary = "apply";
  let description = [{
    ApplyOp represents a function call whose results are passed to
    a continuation.

    An argument of type !schir.value_refs represents splicing all
    of the current call arguments with arguments in that position.
  }];

  let arguments = (ins AnyTypeOf<[Schir_Value, Schir_Procedure]>:$fn,
    Variadic<AnyTypeOf<[Schir_Type, Schir_Rest, Schir_ValueRefs]>>:$args);
  let results   = (outs);
}

def Schir_BindingOp : SchirOp<"binding"> {
  let summary = "binding";
  let description = [{
    BindingOp creates a value wrapper (schir::Binding) that allows
    shared ownership of a reference. This has meaning with SetOp,
    and in the absence of such mutation, the binding may be
    elided and the result can refer directly to the enclosed value.
  }];

  let arguments = (ins Optional<AnyTypeOf<[Schir_Type,
                                           Schir_Rest]>>:$input);
  let results   = (outs Schir_Binding:$result);
  let builders  = [OpBuilder<(ins "::mlir::Value":$input)>];
}

def Schir_CommandOp : SchirOp<"command", [IsolatedFromAbove]> {
  let summary = "command";
  let description = [{
    Command is a node in a sequence that represents
    a single expression whose domain and effects
    would be discarded.

    Currently this is only used for top level expressions.
  }];

  let regions = (region AnyRegion:$body);
  let extraClassDeclaration = [{
    mlir::Block* addEntryBlock() {
      assert(getBody().empty() && "CommandOp already has an entry block");
      mlir::Block* entry = new mlir::Block();
      getBody().push_back(entry);
      return entry;
    }
  }];

  let arguments = (ins /*none*/);
  let results   = (outs /*none*/);
}

def Schir_ConsOp : SchirOp<"cons"> {
  let summary = "cons";
  let description = [{
    ConsOp constructs a pair from two values. That's it.
  }];

  let arguments = (ins Schir_Type:$a, Schir_Type:$b);
  let results   = (outs Schir_Pair:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$a, "::mlir::Value":$b)>
  ];
}

def Schir_ContOp : SchirOp<"cont", [ReturnLike, Terminator]> {
  let summary = "cont";
  let description = [{
    ContOp models calling the current continuation. It can take
    an arbitrary amount of inputs and represents the result(s) of
    an expression.
  }];

  let arguments = (ins Variadic<AnyTypeOf<[Schir_Type, Schir_Syntax]>>:$args);
  let results   = (outs); // no result
}

def Schir_ExpandPacksOp : SchirOp<"expand_packs", []> {
  let description = [{
    Expand a list of packs from one or more subpatterns.
    This is equivalent to calling `map` on a list of packs
    with an input function that emits the output syntax
    as a list.
  }];

  let arguments = (ins Schir_Type:$cdr, Variadic<Schir_Type>:$packs);
  let results = (outs Schir_Value:$result);
  let regions = (region SizedRegion<1>:$body);

  let builders = [
    OpBuilder<(ins "::mlir::Value":$cdr,
                   "::mlir::ValueRange":$packs,
                   "std::unique_ptr<::mlir::Region>&&":$body)>
  ];
}

def Schir_OpGenOp : SchirOp<"opgen", [Terminator]> {
  let summary = "opgen";
  let description = [{
    OpGenOp compiles scheme into the SchirScheme MLIR dialect.
    This operation is used for syntax code generation.
  }];

  let arguments = (ins Schir_Type:$input);
  let results   = (outs);
}

def Schir_GlobalOp : SchirOp<"global", [Symbol, IsolatedFromAbove]> {
  let summary = "global";
  let description = [{
    This indicates a module top level variable providing
    a symbol. If there is no initializer then it is external.
  }];

  let regions = (region AnyRegion:$initializer);
  let extraClassDeclaration = [{
    bool isExternal() { return getInitializer().empty(); }
    mlir::Block* addEntryBlock() {
      assert(getInitializer().empty() && "GlobalOp already has an entry block");
      mlir::Block* entry = new mlir::Block();
      getInitializer().push_back(entry);
      return entry;
    }
  }];

  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type);
}

def Schir_GlobalBindingOp : SchirOp<"global_binding",
                                    [Symbol, IsolatedFromAbove]> {
  let description = [{
    Create a module top level variable and assign it the binding
    encapsulated by a schir::ContextLocal object that is dynamically
    loaded from the provided $ext_sym_name.
  }];

  let arguments = (ins StrAttr:$sym_name, StrAttr:$ext_sym_name);
}

def Schir_GlobalSyntaxOp : SchirOp<"global_syntax",
                                   [Symbol, IsolatedFromAbove]> {
  let description = [{
    GlobalSyntaxOp denotes a module top level syntax object
    that is initialized at compile time.
    If there is not initializer then it is external.
  }];

  let regions = (region AnyRegion:$initializer);
  let extraClassDeclaration = [{
    bool isExternal() { return getInitializer().empty(); }
    mlir::Block* addEntryBlock() {
      assert(getInitializer().empty() && "GlobalOp already has an entry block");
      mlir::Block* entry = new mlir::Block();
      getInitializer().push_back(entry);
      return entry;
    }
  }];

  let arguments = (ins StrAttr:$sym_name);
}

def Schir_IfOp : SchirOp<"if", []> {
  let summary = "if";
  let description = [{
    IfOp handles branching to one of two regions based on an
    input result.
  }];

  let regions   = (region SizedRegion<1>:$thenRegion,
                          SizedRegion<1>:$elseRegion);
  let arguments = (ins Schir_Type:$input);
  let results   = (outs Schir_Type:$result);

  let builders  = [OpBuilder<(ins "::mlir::Value":$input)>];
}

def Schir_IfContOp : SchirOp<"if_cont", [ReturnLike, Terminator]> {
  let summary = "if_cont";
  let description = [{
    IfContOp represents branching where one or more of the branches
    contain an ApplyOp so a continuation must be provided.
  }];

  let regions   = (region SizedRegion<1>:$thenRegion,
                          SizedRegion<1>:$elseRegion);
  let arguments = (ins Schir_Type:$input);
  let results   = (outs);
}


def Schir_LambdaOp : SchirOp<"lambda"> {
  let summary = "lambda";
  let description = [{
    Lambdas represent a closure. They take the name of a function
    and a list of values to capture.
  }];

  let arguments = (ins StrAttr:$name,
                       Variadic<AnyTypeOf<[Schir_Type,
                                           Schir_Binding]>>:$captures);
  let results = (outs Schir_Procedure:$result);

  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$name,
              "::llvm::ArrayRef<::mlir::Value>":$captures)>
  ];
}

def Schir_LiteralOp : SchirOp<"literal", [Pure]> {
  let summary = "literal";
  let description = [{
    Literals in scheme can be primitives or quoted scheme values that are
    known statically and should be immutable.
    For starters this should simply lower to calling the corresponding
    factory function to dynamically allocate the object and return a
    run-time schir::Value.
  }];

  let arguments = (ins Schir_ValueAttr:$input);
  let results   = (outs Schir_Type:$result);
  let builders = [
    OpBuilder<(ins "::schir::Value":$input)>
  ];
}

def Schir_LoadRefOp : SchirOp<"load_ref"> {
  let summary = "load_ref";
  let description = [{
    Read an element from a ValueRefs object which can be
    the context's callee capture list or a variadic argument
    list which is primarily used for continuations.
  }];

  let arguments = (ins AnyTypeOf<[Schir_Context, Schir_ValueRefs]>:$value_refs,
                       UI32Attr:$index);
  let results   = (outs Schir_ValueOrBinding:$result);

  let builders = [
    OpBuilder<(ins "::mlir::Value":$closure, "uint32_t":$index)>
  ];
}

def Schir_LoadGlobalOp : SchirOp<"load_global"> {
  let summary = "load_global";
  let description = [{
    Read the value from a global.
  }];

  let arguments = (ins FlatSymbolRefAttr:$name);
  let results   = (outs Schir_ValueOrBinding:$result);

  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$name)>
  ];
}

def Schir_LoadModuleOp : SchirOp<"load_module"> {
  let summary = "load_module";
  let description = [{
    Load a module by its mangled name and initialize it.
    This also enables the creation of a runtime dependency graph
    for exporting to bytecode files.
  }];

  let arguments = (ins FlatSymbolRefAttr:$name);
  let results   = (outs);

  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$name)>
  ];
}

def Schir_ExportIdOp : SchirOp<"export-id"> {
  let summary = "export-id";
  let description = [{
    Provide an identifier to associate with a target identifier $id
    with a mangled name $symbolName.
  }];
  let arguments = (ins FlatSymbolRefAttr:$symbolName, StrAttr:$id,
                   StrAttr:$origId);
}

def Schir_MatchOp : SchirOp<"match", []> {
  let summary = "match";
  let description = [{
    As a component of pattern matching, this checks if a value
    is equal (in terms of `equal?`) to the input attribute.
    The attribute should be a leaf constant value.
  }];

  let arguments = (ins Schir_ValueAttr:$val, Schir_Type:$input);
  let results   = (outs);

  let builders = [
    OpBuilder<(ins "::schir::Value":$val, "::mlir::Value":$input)>
  ];
}

def Schir_MatchIdOp : SchirOp<"match-id", []> {
  let summary = "match-id";
  let description = [{
    Perform matching of a non-literal value and input value such that they
    have the same binding or pointer address.
    (Id means identifier.)
  }];

  let arguments = (ins Schir_Type:$val, Schir_Type:$input);
  let results   = (outs);
}

def Schir_MatchPairOp : SchirOp<"match_pair", []> {
  let summary = "match_pair";
  let description = [{
    As a component of pattern matching, this checks if a value
    is a pair and destructures it.
  }];

  let arguments = (ins Schir_Type:$input);
  let results   = (outs Schir_Type:$car, Schir_Type:$cdr);

  let builders = [
    OpBuilder<(ins "::mlir::Value":$input)>
  ];
}

def Schir_MatchTailOp : SchirOp<"match_tail", []> {
  let description = [{
    Destructure the last N lengthed list of the tail of the input list.
    The last cdr is counted in the length.
    (ie () or any element for improper list.)
    The tail will be matched against a pattern.
    If the tail is a pair, its address will serve as a sentinel
    for terminating the matching of elements against the subpattern.
  }];

  let arguments = (ins UI32Attr:$length,
                       Schir_Type:$input);
  let results   = (outs Schir_Type:$tail);

  let builders = [
    OpBuilder<(ins "uint32_t":$length,
                   "::mlir::Value":$input)>
  ];
}

def Schir_MatchVectorOp : SchirOp<"match_vector", []> {
  let description = [{
    Destructure a vector literal with a single, optional subpattern.
    The attributes $head and $tail are the number of elements before
    and after the subpattern P_e. If P_e does not exist then $tail is 0.
    The number of results are $head + $tail + 1 or $head if $tail is 0.
    The results are ordered (H... E_e T....) where H and T are the
    corresponding head and tail elements and E_e is a list of the
    elements to match against the subpattern.
  }];

  let arguments = (ins UI32Attr:$head,
                       UI32Attr:$tail,
                       Schir_Type:$input);
  let results   = (outs Variadic<Schir_Type>:$results);

  let builders = [
    OpBuilder<(ins "uint32_t":$head,
                   "uint32_t":$tail,
                   "::mlir::Value":$input)>
  ];
}

def Schir_MatchArgsOp : SchirOp<"match_args"> {
  let description = [{
    Destructure a dynamically lengthed array (ValueRefs) using the type
    of a function. This matches arity and types of its arguments
    and should support rest arguments.

    (Use for implementing case-lambda.)
  }];

  let arguments = (ins Schir_ValueRefs:$valueRefs);
  let results   = (outs Variadic<AnyTypeOf<[Schir_Type,
                                            Schir_Rest]>>:$results);

  let builders = [
    OpBuilder<(ins "::mlir::FunctionType":$functionType,
                   "::mlir::Value":$valueRefs)>
  ];
}

def Schir_MatchTypeOp : SchirOp<"match_type", []> {
  let description = [{
    Perform dynamic cast resulting in a value with a specified type.
    Like other Match operations this will raise an error if the match
    fails unless it is in the body of a PatternOp.
  }];

  let arguments = (ins Schir_ValueOrBinding:$arg);
  let results   = (outs Schir_Type:$result);
}

def Schir_SubpatternOp : SchirOp<"subpattern", []> {
  let description = [{
    Match a list of elements over a subpattern that
    precedes the `...` ellipsis. Each pattern variable within
    the list is returned as a pack list that may only be expanded
    by a subtemplate.

    We stop at the elements that occur in the list after the
    subpattern by passing the tail value as a sentinel.
    It checks by the address of the pair or any cdr that is
    not a pair.
  }];

  let arguments = (ins Schir_Type:$input, Schir_Type:$tail);
  let results   = (outs Variadic<Schir_Type>:$packs);
  let regions   = (region SizedRegion<1>:$body);

  let builders = [
    OpBuilder<(ins "::mlir::Value":$input,
                   "::mlir::Value":$tail,
                   "std::unique_ptr<::mlir::Region>&&":$body,
                   "unsigned":$num_packs)>
  ];
}

def Schir_ResolveOp : SchirOp<"resolve", [Terminator]> {
  let description = [{
    Terminate the region of a SubpatternOp or ExpandPacksOp.
    SubpatternOp takes the created pattern variables as $args.
    ExpandPacksOp creates the generated list as its single
    argument.
  }];

  let arguments = (ins Variadic<Schir_Type>:$args);
  let results   = (outs);
}

def Schir_PatternOp : SchirOp<"pattern", []> {
  let description = [{
    PatternOp represents a pattern/template pair as part of the
    syntax-rules transform specifier. Operations in its region are
    evaluated in order. When a match operation fails within its region,
    evaluation will jump to the successor operation of the containing
    PatternOp (which is likely another pattern or error.)

    Successfully matched patterns will likely generate and compile a
    new scheme form via ConsOps and terminating with an OpGenOp.
    Alternatively they may terminate with an ErrorOp in the case
    of `syntax-error`.
  }];

  let arguments = (ins);
  let results   = (outs);
  let regions   = (region SizedRegion<1>:$region);
}

def Schir_PushContOp : SchirOp<"push_cont", []> {
  let summary = "push_cont";
  let description = [{
    Creates a closure for a continuation object to be
    pushed to the continuation stack. This will typically
    precede an ApplyOp or other "non-tail" operation.
  }];

  let arguments = (ins StrAttr:$name,
                       Variadic<Schir_ValueOrBinding>:$captures);
  let results   = (outs);
}

def Schir_RenameOp : SchirOp<"rename"> {
  let summary = "rename";
  let description = [{
    Capture a value and "rename" it using an identifier to use in the
    scope of a syntax object. These lookups can be shadowed by the
    environment created during template expansion (as I understand it.)

    The $capture mlir::Value which may belong to a parent scope is
    used to generate a localized value in the context of the template
    expansion.
  }];

  let arguments = (ins StrAttr:$id,
        AnyTypeOf<[Schir_ValueOrBinding,
                   Schir_Syntax]>:$capture);
  let results   = (outs
        AnyTypeOf<[Schir_ValueOrBinding,
                   Schir_Syntax]>:$result);
  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$id, "::mlir::Value":$capture)>
  ];
}

def Schir_RenameGlobalOp : SchirOp<"rename_global"> {
  let summary = "rename_global";
  let description = [{
    Similar to RenameOp, this RenameGlobalOp represents the possibility of
    expanding the use of a global by generating a LoadGlobalOp in the context
    of the template expansion.

    Because globals can be defined lazily within the module, it is possible
    that this operation could be generated when no global of the corresponding
    name was defined. In this case the RenameOp can be removed.
  }];

  let arguments = (ins StrAttr:$id, StrAttr:$sym);
  let results   = (outs Schir_ValueOrBinding:$result);
  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$id, "::llvm::StringRef":$sym)>
  ];
}

def Schir_RenameEnvOp : SchirOp<"rename_env"> {
  let description = [{
    RenameEnvOp creates a new EnvFrame with $bindings
    which serves as the tail of a new environment stack
    with the most local EnvFrame from $env on top.

    The local EnvFrame holds the local stack so any syntax closure
    created will be topped with any newly added local variables
    or EnvFrames automatically.
  }];

  let arguments = (ins Schir_Type:$env, Variadic<Schir_ValueOrBinding>:$bindings);
  let results   = (outs Schir_Type:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$env,
                   "::llvm::ArrayRef<::mlir::Value>":$args)>
  ];
}

def Schir_SourceLocOp : SchirOp<"source-loc"> {
  let summary = "source-loc";
  let description = [{
    Get the source location from a Schir_Type object.
    This is primarily used to propagate source locations
    of the inputs to pattern variables.
  }];

  let arguments = (ins Schir_Type:$arg);
  let results   = (outs Schir_Type:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$arg)>
  ];
}

def Schir_SetOp : SchirOp<"set"> {
  let summary = "set";
  let description = [{
    SetOp sets the enclosed value of a binding. This is used
    by the `set` syntax as well as lazy initialization of local
    variables needed by `letrec` and similar binding constructs.
  }];

  let arguments = (ins AnyTypeOf<[Schir_Binding, Schir_Unknown]>:$binding,
                                  Schir_Type:$input);
  let results   = (outs Schir_Type:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$binding, "::mlir::Value":$input)>
  ];
}

def Schir_SpliceOp : SchirOp<"splice"> {
  let summary = "splice";
  let description = [{
    SpliceOp splices two lists or vectors. The result type
    should be the same as second operand.
    TODO Vectors and types are not supported yet.
  }];

  let arguments = (ins Schir_Type:$a, Schir_Type:$b);
  let results   = (outs Schir_Type:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$a, "::mlir::Value":$b)>
  ];
}

def Schir_SyntaxClosureOp : SchirOp<"syntax_closure"> {
  let summary = "syntax_closure";
  let description = [{
    SyntaxClosureOp captures a syntax node with the given
    environment. This supports generating code during hygienic
    macro expansion.
  }];

  let arguments = (ins Schir_Type:$sourceVal, Schir_Type:$input,
                       Schir_Type:$env);
  let results   = (outs Schir_Type:$result);
  let builders  = [
    OpBuilder<(ins "::mlir::Value":$sourceVal, "::mlir::Value":$input,
                   "::mlir::Value":$env)>
  ];
}

def Schir_SyntaxOp: SchirOp<"syntax", []> {
  let summary = "syntax";
  let description = [{
    SyntaxOp creates a syntax function object that is callable like
    a lambda but has the specific purpose of generating new
    operations in the current builder context.

    The argument $name refers to the function name.
  }];

  let arguments = (ins StrAttr:$name);
  let results = (outs Schir_Syntax:$result);

  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$name)>
  ];
}

def Schir_ToVectorOp : SchirOp<"to_vector"> {
  let summary = "to_vector";
  let description = [{
    ToVectorOp converts a list into a vector.
    This is used for the simplification of quasiquote
    of vector literals.
  }];

  let arguments = (ins Schir_Type:$input);
  let results   = (outs Schir_Vector:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$input)>
  ];
}

def Schir_VectorOp : SchirOp<"vector"> {
  let summary = "vector";
  let description = [{
    VectorOp creates a schir::Vector from its arguments.
  }];

  let arguments = (ins Variadic<Schir_Type>:$args);
  let results   = (outs Schir_Vector: $result);
  let builders = [
    OpBuilder<(ins "::llvm::ArrayRef<::mlir::Value>":$args)>
  ];
}

def Schir_UndefinedOp : SchirOp<"undefined", [Pure]> {
  let summary = "undefined";
  let description = [{
    This is a singleton literal for schir::Undefined
  }];

  let arguments = (ins);
  let results   = (outs Schir_Undefined:$result);
  let builders = [
    OpBuilder<(ins)>
  ];
}

def Schir_UnboxOp : SchirOp<"unbox", [Pure]> {
  let description = [{
    Convert a reference to its referenced value.
  }];

  let arguments = (ins AnyTypeOf<[Schir_Binding, Schir_Unknown]>:$binding);
  let results   = (outs Schir_Value:$result);
}

def Schir_InitGlobalOp : SchirOp<"init_global", []> {
  let description = [{
    Register a global variable with a binding or initial value.
  }];

  let arguments = (ins FlatSymbolRefAttr:$name,
                       AnyTypeOf<[Schir_Binding, Schir_Type]>:$var);
  let results   = (outs);
}

#endif // SCHIR_DIALECT_TD
