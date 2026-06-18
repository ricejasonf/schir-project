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

class Geomalg_Trait<string name, list<Trait> traits = [],
                    code extraOpDeclaration = [{}],
                    code extraOpDefinition = [{}]>
    : NativeOpTrait <name, traits, extraOpDeclaration, extraOpDefinition> {
  let cppNamespace = "::geomalg";
}

// If any operand is Multivector, expand and distribute.
def Distributive : Geomalg_Trait<"Distributive">;

// The stuff we can turn into matrices.
def Multilinear : Geomalg_Trait<"Multilinear">;

// If any operand is Zero, result is Zero.
def ZeroAbsorb : Geomalg_Trait<"ZeroAbsorb">;

// If operation multiplies scalars convertible to CMul
def IsMul : Geomalg_Trait<"IsMul">;

def InferredResult : TraitList<[
  DeclareOpInterfaceMethods<InferTypeOpInterface>,
  Geomalg_Trait<"InferredResult", [],
    /*extraOpDeclaration=*/[{
      using InferredResultBase::isCompatibleReturnTypes;
    }]>
  ]>;

// Type interfaces

def Geomalg_MultivectorLike : TypeInterface<"MultivectorLike"> {
  let description = [{
    Provide an interface for tuple like objects representing sums
    of basis blades. This allows for different types to encode
    different properties such as unit length which allow for
    omitting certain operations such as division by 1.
  }];
  let cppNamespace = "geomalg";
  let methods = [
    InterfaceMethod<
      [{Get the BladeTypes}],
      /*ReturnType*/"llvm::ArrayRef<geomalg::BladeType>",
      /*MethodName*/"getBlades">,
    InterfaceMethod<[{
      Check if multivector is known to have unit length (ie ∑( cᵢ)² = 1)
      }],
      /*ReturnType*/"bool",
      /*MethodName*/"isUnit">,
    InterfaceMethod<[{
      Check if multivector is known to be a sum of basis 1-blades.
      }],
      /*ReturnType*/"bool",
      /*MethodName*/"isVector">
  ];
}

// Types

def Geomalg_Zero : TypeDef<Geomalg_Dialect, "Zero", []> {
  let mnemonic = "zero";
  let description = [{
    A blade of scalar zero has the zero type (iff its statically known.)
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

    geomalg::BladeType oswap() const {
      return get(getContext(), getBladeTag().oswap().getTag());
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

def Geomalg_Multivector : TypeDef<Geomalg_Dialect, "Multivector",
                                  [Geomalg_MultivectorLike]> {
  let mnemonic = "multivector";
  let description = [{
    A multivector is a sum of basis blades. It is itself a k-blade if
    it can be factored as the outer product of k vectors.
  }];

  let parameters = (ins ArrayRefParameter<"geomalg::BladeType">:$blades);

  let assemblyFormat = "`<` $blades `>`";
  let extraClassDeclaration = [{
    bool isUnit() const { return false; }
    bool isVector() const;
  }];
}

def Geomalg_Unitvector : TypeDef<Geomalg_Dialect, "UnitVector",
                                 [Geomalg_MultivectorLike]> {
  let mnemonic = "unit_vector";
  let description = [{
    A unit vector is a sum of basis 1-blades with the assumed property
    that A^2 = 1 (assuming an orthonormal basis.)

    We also get the properties:
      A⁻¹ = A (inverse)
      A^ = -A (grade involution)
      A~ = A  (reverse)
  }];

  let parameters = (ins ArrayRefParameter<"geomalg::BladeType">:$blades);
  let assemblyFormat = "`<` $blades `>`";
  let extraClassDeclaration = [{
    bool isUnit() const { return true; }
    bool isVector() const { return true; }
    static llvm::LogicalResult verifyInvariants(
              auto EmitError, llvm::ArrayRef<geomalg::BladeType> Blades) {
      return llvm::success(llvm::all_of(Blades,
        [](geomalg::BladeType BT) { return BT.getGrade() == 1; }));
    }
  }];
}

def Geomalg_Unknown : TypeDef<Geomalg_Dialect, "Unknown", []> {
  let mnemonic = "unknown";
  let description = [{
    A blade or multivector whose type must be inferred.
  }];
}

def Geomalg_Value : AnyTypeOf<[Geomalg_Blade,
                               Geomalg_MultivectorLike,
                               Geomalg_Zero,
                               Geomalg_Unknown]>;

// Operations

class Geomalg_Op<string mnemonic, list<Trait> traits = []> :
    Op<Geomalg_Dialect, mnemonic, traits # [Pure]>;

def Geomalg_SumOp : Geomalg_Op<"sum", [InferredResult]> {
  let description = [{
    Take a sum of k-blades to produce a multivector.

    If the $IsUnit attribute is set the sum returns a unit_vector.
    This is known to happen when negating a unit_vector.
  }];

  let arguments = (ins UnitAttr:$IsUnit, Variadic<Geomalg_Value>:$args);
  let results   = (outs Geomalg_Value:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$LHS, "::mlir::Value":$RHS), [{
        std::array<mlir::Value, 2> Operands{{LHS, RHS}};
        build($_builder, $_state, Operands);
      }]>
  ];
}

def Geomalg_ExpandOp : Geomalg_Op<"expand", [InferredResult]> {
  let description = [{
    Expand breaks out the operands of a multivector's
    sum of basis blades representation.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Variadic<Geomalg_Blade>:$results);
}

def Geomalg_MatvecOp : Geomalg_Op<"matvec",
      [ZeroAbsorb, InferredResult]> {
  let description = [{
    Create a matrix of a linear operator by defining each
    column vector with its own region.

    The regions are likely copied from a template region with
    the first argument mapped to a unit basis vector for
    each basis vector in the original input multivector.
    The sole operand of this operation is the original input
    multivector and the operation itself represents the created
    matrix left multiplied by the operand.
  }];

  let regions = (region VariadicRegion<AnyRegion>:$bodies);
  let arguments = (ins Geomalg_Value: $arg);
  let results = (outs Geomalg_Value: $result);

  let extraClassDeclaration = [{
    // Get number of rows in matrix.
    int64_t getRowDimSize() {
      if (auto MV = dyn_cast<MultivectorLike>(getResult().getType()))
        return MV.getBlades().size();
      else
        return 1;
    }

    // Get number of columns in matrix.
    int64_t getColDimSize() {
      return getBodies().size();
    }
  }];
}

def Geomalg_OuterProdOp : Geomalg_Op<"oprod",
      [Multilinear, Distributive, ZeroAbsorb, InferredResult, IsMul]> {
  let description = [{
    Map two multivectors to a multivector by their
    wedge product.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_InnerProdOp : Geomalg_Op<"iprod",
      [Multilinear, Distributive, ZeroAbsorb, IsMul, InferredResult]> {
  let description = [{
    Map two multivectors to a scalar by their interior product.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_GeomProdOp : Geomalg_Op<"gprod",
              [Multilinear, Distributive, ZeroAbsorb]> {
  let description = [{
    Map two multivectors to the sum of their interior
    and exterior products which produces a multivector.
  }];

  let arguments = (ins Geomalg_Value:$LHS,
                       Geomalg_Value:$RHS);
  let results   = (outs Geomalg_Unknown:$result);
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
      [Multilinear, Distributive, InferredResult]> {
  let description = [{
    Take the reverse of a k-blade that swaps vectors
    in the wedge product until the vectors are in reverse sorted
    order. The only effect of this operation may be a sign
    change depending on the grade of the k-blade.
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_GradeInvoOp : Geomalg_Op<"grade_invo",
      [Multilinear, Distributive, InferredResult]> {
  let description = [{
    Perform grade involution on a k-blade which will
    negate elements when 2k + 1 ≅ 0 (mod 2).
  }];

  let arguments = (ins Geomalg_Value:$arg);
  let results   = (outs Geomalg_Value:$result);
}

def Geomalg_NegateOp : Geomalg_Op<"negate",
      [Multilinear, Distributive, ZeroAbsorb, InferredResult]> {
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

def Geomalg_OSwapOp : Geomalg_Op<"oswap",
      [ZeroAbsorb, InferredResult]> {
  let description = [{
    Convert a basis blade of grade k > 1 to its anitsymmetric
    counterpart. This changes the canonicalness of the type
    and negates the coefficient.  If the blade is a basis
    1-vector then the operation behaves as the identity.
  }];

  let arguments = (ins Geomalg_Blade:$arg);
  let results   = (outs Geomalg_Blade:$result);
}

def Geomalg_CastOp : Geomalg_Op<"cast", [ZeroAbsorb]> {
  let description = [{
    Cast a blade to another blade type that is typically
    used to factor out a basis vector from a k-blade.
    This operation does not change the the value (scalar coefficient).
    When factoring a noncanonical wedge product, negate will
    be used in addition to this operation.
  }];

  let arguments = (ins AnyType:$arg);
  let results   = (outs AnyType:$result);
}

def Geomalg_ConvertOp : Geomalg_Op<"convert"> {
  let description = [{
    Convert can be used to refine an object such that it
    posseses properties indicated by the type it is converting to.
    For instance, to convert a vector to a unit_vector, a pattern
    rewriter can expand this operation into a division by the square
    of the vector followed by a cast which would guarantee that
    the result squared would equal 1 and subsequent transformations
    could make that assumption based on the type.
  }];

  let arguments = (ins AnyType:$arg);
  let results   = (outs AnyType:$result);
}

def Geomalg_CMulOp : Geomalg_Op<"cmul", [ZeroAbsorb, IsMul]> {
  let description = [{
    Multiply the coefficients of multiple blades to produce
    a scalar multiple of some other blade.

    This is typically generated after applying a metric and
    replaces both oprod and iprod along with any casts.
  }];

  let arguments = (ins Variadic<Geomalg_Blade>:$args);
  let results   = (outs Geomalg_Blade:$result);
}

def Geomalg_DotOp : Geomalg_Op<"dot", [ZeroAbsorb]> {
  let description = [{
    Take the inner product of two vectors that
    we know share orthogonal bases.
  }];

  let arguments = (ins Geomalg_Value:$LHS, Geomalg_Value:$RHS);
  let results   = (outs Geomalg_BladeOfGradeK<0>:$result);
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
        build($_builder, $_state, resultType,
              $_builder.getF32FloatAttr(val));
      }]>,
    OpBuilder<(ins CArg<"float">:$val), [{
        mlir::Type ScalarT
          = geomalg::BladeType::get($_builder.getContext(), 0);
        build($_builder, $_state, ScalarT,
              $_builder.getF32FloatAttr(val));
      }]>
  ];

  let extraClassDeclaration = [{
    bool isOne() {
      llvm::APFloat C = getCoefficient().getValue();
      return C.bitwiseIsEqual(llvm::APFloat::getOne(C.getSemantics()));
    }

    float getFloat() {
      llvm::APFloat C = getCoefficient().getValue();
      return C.convertToFloat();
    }
  }];
}

def Geomalg_VersorProdOp : Geomalg_Op<"vprod", [Multilinear, ZeroAbsorb]> {
  let description = [{
    Given an ordered set of invertible blades, construct a versor
    and its inverse applying it to an operand using the
    sandwich product.
  }];

  let arguments = (ins Geomalg_Value:$arg, Variadic<Geomalg_Value>:$versors);
  let results   = (outs Geomalg_Unknown:$result);
  let builders = [
    OpBuilder<(ins "::mlir::Value":$Arg, "::mlir::ValueRange":$Versors), [{
        $_state.addOperands(Arg);
        $_state.addOperands(Versors);
        $_state.addTypes(geomalg::UnknownType::get($_builder.getContext()));
      }]>
  ];
}

def Geomalg_ReturnOp : Op<Geomalg_Dialect, "return", [Terminator]> {
  let description = [{
    Provide a return-like operation that does not verify types
    so we can deduce return types in a subsequent pass.
  }];

  let arguments = (ins AnyType:$arg);

  let builders = [
    OpBuilder<(ins), [{ build($_builder, $_state, {}); }]>
  ];

  let assemblyFormat = "attr-dict $arg `:` type($arg)";
}

def CallOp : Op<Geomalg_Dialect, "call", []> {
  let description = [{
    Provide a call operation that does not verify matching types
    so we can deduce argument and return types and possibly instantiate
    functions based on call expressions.
  }];

  let arguments = (ins
    FlatSymbolRefAttr:$callee,
    Variadic<AnyType>:$args
  );

  let results = (outs AnyType:$result);

  let builders = [
    OpBuilder<(ins "mlir::SymbolRefAttr":$callee, "mlir::Type":$result,
      CArg<"mlir::ValueRange", "{}">:$operands), [{
      $_state.addOperands(operands);
      $_state.addAttribute("callee", callee);
      $_state.addTypes(result);
    }]>
  ];

  let assemblyFormat = "$callee `(` $args `)` attr-dict `:` functional-type($args, $result)";
}

#endif // GEOMALG_DIALECT_TD
