// Geomalg.td - MLIR Dialect Definitions for Geomalg
// Copyright Jason Rice 2025
//
#ifndef GEOMALG_DIALECT_TD
#define GEOMALG_DIALECT_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def Geomalg_Dialect : Dialect {
  let name = "geomalg";
  let useDefaultTypePrinterParser = 1;
}

// Traits

class Geomalg_Trait<string name> : NativeOpTrait<name> {
  let cppNamespace = "::geomalg";
}

// If any operand is Multivector, expand and distribute.
def Distributive : Geomalg_Trait<"Distributive">;

// If any operand is Zero, result is Zero.
def ZeroAbsorb : Geomalg_Trait<"ZeroAbsorb">;

def InferredResult : DeclareOpInterfaceMethods<InferTypeOpInterface>;

// Types

def Geomalg_Metric : TypeDef<Geomalg_Dialect, "Metric", []> {
  let mnemonic = "metric";
  let description = [{
    Define a metric tensor by specifying the dot product for pairs of basis
    vectors defaulting to e_i . e_i = 1 and e_i . e_j = 0 for i != j.

    TODO finish this
  }];

  //let parameters = (ins ArrayRefParameter<"geomalg::MetricEntry">:$entries);
  let assemblyFormat = "`<` params `>`";
  let extraClassDeclaration = [{

  }];
}

def Geomalg_Zero : TypeDef<Geomalg_Dialect, "Zero", []> {
  let mnemonic = "zero";
  let description = [{
    A blade of scalar zero has the zero type (iff its statically known.)
  }];

  let extraClassDeclaration = [{
    geomalg::BladeTag getCanonicalTag() const { return {}; }
    unsigned getGrade() const { return 0; }
    bool isBasisVector() const { return true; }
    static constexpr bool isZero() { return true; }
    static constexpr bool isCanonical() { return true; }
  }];
}

def Geomalg_Blade : TypeDef<Geomalg_Dialect, "Blade", []> {
  let mnemonic = "blade";
  let description = [{
    A basis k-blade is a wedge product of standard basis vectors
    where k is the number of vectors in the product.

    The type is parameterized by a bitmap tag to indicate what elements are
    present in the wedge product by their power of two.
    e.g.  A bitmap value of 0 would represent a scalar
          where 7 (0111) would represent e_1^e_2^e_3.
          err...
          0111
          0*2^3 + 1*2^2 + 1*2^1 + 1*2^0 = 4 + 2 + 1 = 7
          Yes.

    Additionally, we use the high bit to indicate the sign which can
    denote the order of the elements in the wedge product
    (though not uniquely).
    e.g.  e_1^e_2^e_3 = - e_2^e_1^e_3

    With this, we establish the convention that product operands
    in sorted order have positive sign.
  }];

  let parameters = (ins "uint32_t":$tag);

  // Try to remember that the argument is a tag and not the grade.
  let assemblyFormat = "`<` $tag `>`";

  let extraClassDeclaration = [{
    geomalg::BladeTag getBladeTag() const {
      return geomalg::BladeTag(getTag());
    }

    std::strong_ordering operator<=>(BladeType B) const {
      return getBladeTag().operator<=>(B.getBladeTag());
    }

    geomalg::BladeTag getCanonicalTag() const {
      return getBladeTag().getCanonical();
    }

    unsigned getGrade() const {
      return getBladeTag().getGrade();
    }

    geomalg::BladeType getCanonicalType() const {
      return get(getContext(), getCanonicalTag().getTag());
    }

    bool isCanonical() const {
      return getBladeTag().isCanonical();
    }

    bool shouldInvoNegate() const {
      return getBladeTag().shouldInvoNegate();
    }

    bool shouldReverseNegate() const {
      return getBladeTag().shouldReverseNegate();
    }

    // See BladeTag::factor().
    std::pair<BladeType, BladeType> factor() const {
      auto [L, R] = getBladeTag().factor();
      return {get(getContext(), L.getTag()),
              get(getContext(), R.getTag())};
    }

    bool isZero() const { 
      // Instead of adding a bunch of run-time verification,
      // we will support zero but assert against it here.
      assert(!getBladeTag().isZero());
      return getBladeTag().isZero();
    }
  }];
}

class Geomalg_BladeOfGradeK<int K> :
  Type<
    CPred<
      "::llvm::cast<geomalg::BladeType>($_self).getGrade() == " # K>>;

def Geomalg_Multivector : TypeDef<Geomalg_Dialect, "Multivector", []> {
  let mnemonic = "multivector";
  let description = [{
    A multivector is a sum of basis blades. It is itself a k-blade if
    it can be factored as the outer product of k vectors.
  }];

  let parameters = (ins ArrayRefParameter<"geomalg::BladeType">:$blades);

  let assemblyFormat = "`<` $blades `>`";
  let extraClassDeclaration = [{
    bool isBlade(unsigned K) const;
  }];
}

def Geomalg_Unknown : TypeDef<Geomalg_Dialect, "Unknown", []> {
  let mnemonic = "unknown";
  let description = [{
    A blade or multivector whose type must be inferred.
  }];
}

def Geomalg_Value : AnyTypeOf<[Geomalg_Blade,
                               Geomalg_Multivector,
                               Geomalg_Zero,
                               Geomalg_Unknown]>;

// Operations

class Geomalg_Op<string mnemonic, list<Trait> traits = []> :
    Op<Geomalg_Dialect, mnemonic, traits # [Pure]>;

def Geomalg_SumOp : Geomalg_Op<"sum", [InferredResult]> {
  let description = [{
    Take a sum of k-blades to produce a multivector.
  }];

  let arguments = (ins Variadic<Geomalg_Value>:$args);
  let results   = (outs Geomalg_Value:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$LHS, "::mlir::Value":$RHS), [{
        std::array<mlir::Value, 2> Operands{{LHS, RHS}};
        build($_builder, $_state, Operands);
      }]>
  ];
}

def Geomalg_ExpandOp : Geomalg_Op<"expand", []> {
  let description = [{
    Expand breaks out the operands of a multivector's
    sum of basis blades representation.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Variadic<Geomalg_Blade>:$results);
}

def Geomalg_OuterProdOp : Geomalg_Op<"oprod",
      [Distributive, ZeroAbsorb, InferredResult]> {
  let description = [{
    Map two multivectors to a multivector by their
    wedge product.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_InnerProdOp : Geomalg_Op<"iprod", [Distributive, ZeroAbsorb]> {
  let description = [{
    Map two multivectors to a scalar by their interior product.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Value:$result);

  let extraClassDeclaration = [{
    static mlir::Type maybeInferType(mlir::Type, mlir::Type);
  }];

  let builders = [
    OpBuilder<(ins "::mlir::Value":$LHS, "::mlir::Value":$RHS), [{
        // Detect the type for known cases.
        $_state.addOperands(LHS);
        $_state.addOperands(RHS);

        mlir::Type T = maybeInferType(LHS.getType(), RHS.getType());
        $_state.addTypes(T);
      }]>
  ];
}

def Geomalg_GeomProdOp : Geomalg_Op<"gprod", [Distributive, ZeroAbsorb]> {
  let description = [{
    Map two multivectors to the sum of their interior
    and exterior products which produces a multivector.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Value:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$LHS, "::mlir::Value":$RHS), [{
        $_state.addOperands(LHS);
        $_state.addOperands(RHS);
        $_state.addTypes(geomalg::UnknownType::get($_builder.getContext()));
      }]>
  ];
}

def Geomalg_InverseOp : Geomalg_Op<"inverse", [InferredResult]> {
  let description = [{
    Take the multiplicative inverse. For a single k-blade,
    this is defined as applying the multiplicative inverse of
    the norm squared to the reverse of the k-blade.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_ReverseOp : Geomalg_Op<"reverse",
      [Distributive, SameOperandsAndResultType]> {
  let description = [{
    Take the reverse of a k-blade that swaps vectors
    in the wedge product until the vectors are in reverse sorted
    order. The only effect of this operation may be a sign
    change depending on the grade of the k-blade.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_NegateOp : Geomalg_Op<"negate",
      [Distributive, ZeroAbsorb]> {
  let description = [{
    Apply scalar multiplication of -1 to a blade. This is used
    explicitly in place of grade involution and also when folding
    operations that can be reduced to a simple sign change or
    wherever else there is a sign change which likely depends
    on grade parity.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_CastOp : Geomalg_Op<"cast", [ZeroAbsorb]> {
  let description = [{
    Cast a blade to another blade type that is typically
    used to factor out a basis vector from a k-blade.
    To change the sign, see NegateOp.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_BladeOp : Geomalg_Op<"blade"> {
  let description = [{
    Construct a basis blade or zero with a constant coefficient.
  }];

  let arguments = (ins Builtin_FloatAttr:$coefficient);
  let results   = (outs Geomalg_Value:$result);

  let builders = [
    // Default to 1.0 for the coefficient.
    OpBuilder<(ins "::mlir::Type":$resultType, CArg<"float", "1.0f">:$val), [{
        //$_state.addAttribute("attr", $_builder.getF32FloatAttr(val));
        build($_builder, $_state, resultType,
              $_builder.getF32FloatAttr(val));
      }]>
  ];

  let extraClassDeclaration = [{
    bool isOne() {
      llvm::APFloat C = getCoefficient().getValue();
      return C.bitwiseIsEqual(llvm::APFloat::getOne(C.getSemantics()));
    }
  }];
}

def Geomalg_VersorProdOp : Geomalg_Op<"vprod", [ZeroAbsorb]> {
  let description = [{
    A versor is a product of invertible vectors and is,
    therefore invertible. However, not all versors are k-blades
    so we keep things in factored form to construct the inverse.

    Given an ordered set of invertible blades, construct a versor
    and its inverse applying it to an operand using the
    sandwich product. Think of each blade being applied as
    transformations ordered from left to right.
  }];

  let arguments = (ins Geomalg_Value:$arg, Variadic<Geomalg_Value>:$blades);
  let results   = (outs Geomalg_Value:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$Arg, "::mlir::ValueRange":$Blades), [{
        $_state.addOperands(Arg);
        $_state.addOperands(Blades);
        $_state.addTypes(geomalg::UnknownType::get($_builder.getContext()));
      }]>
  ];
}

def Geomalg_ReturnOp : Op<Geomalg_Dialect, "return", [ReturnLike,
                                                      Terminator]> {
  let description = [{
    Provide a return-like operation that does not verify types
    so we can deduce return types in a subsequent pass.
  }];

  let arguments = (ins Variadic<AnyType>:$operands);

  let builders = [
    OpBuilder<(ins), [{ build($_builder, $_state, {}); }]>
  ];

  let assemblyFormat = "attr-dict ($operands^ `:` type($operands))?";
}

def CallOp : Op<Geomalg_Dialect, "call", []> {
  let description = [{
    Provide a call operation that does not verify matching types
    so we can deduce argument and return types and possibly instantiate
    functions based on call expressions.
  }];

  let arguments = (ins
    FlatSymbolRefAttr:$callee,
    Variadic<AnyType>:$operands
  );

  let results = (outs Variadic<AnyType>);

  let builders = [
    OpBuilder<(ins "mlir::SymbolRefAttr":$callee, "mlir::TypeRange":$results,
      CArg<"mlir::ValueRange", "{}">:$operands), [{
      $_state.addOperands(operands);
      $_state.addAttribute("callee", callee);
      $_state.addTypes(results);
    }]>
  ];

  let extraClassDeclaration = [{
    mlir::FunctionType getCalleeType();

    /// Get the argument operands to the called function.
    operand_range getArgOperands() {
      return {arg_operand_begin(), arg_operand_end()};
    }

    mlir::MutableOperandRange getArgOperandsMutable() {
      return getOperandsMutable();
    }

    operand_iterator arg_operand_begin() { return operand_begin(); }
    operand_iterator arg_operand_end() { return operand_end(); }

    /// Return the callee of this operation.
    mlir::CallInterfaceCallable getCallableForCallee() {
      return (*this)->getAttrOfType<mlir::SymbolRefAttr>("callee");
    }

    /// Set the callee for this operation.
    void setCalleeFromCallable(mlir::CallInterfaceCallable callee) {
      (*this)->setAttr("callee", cast<mlir::SymbolRefAttr>(callee));
    }
  }];
}

#endif // GEOMALG_DIALECT_TD
