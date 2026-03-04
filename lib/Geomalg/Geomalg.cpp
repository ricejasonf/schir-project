#include <geomalg/Dialect.h>
#include <geomalg/Type.h>
#include <heavy/Context.h>
#include <heavy/MlirHelper.h>
#include <heavy/Value.h>

extern "C" {
void geomalg_init(heavy::Context& C, heavy::ValueRefs) {
  C.DialectRegistry->insert<geomalg::GeomalgDialect>();
  C.Cont();
}

void geomalg_basis_vector_type(heavy::Context& C, heavy::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (!isa<heavy::Int>(Args[0]))
    return C.RaiseError("expecting integer: {}", Args[0]);

  uint32_t Tag = static_cast<uint32_t>(cast<heavy::Int>(Args[0]));

  // A basis vector must be a power of 2 that is
  // not highest representable power of 2.
  // This also means it is does not contain a wedge
  // product of two nontrivial vectors.
  if (std::popcount(Tag) > 1)
    return C.RaiseError("expecting power of two: {}", Args[0]);

  mlir::MLIRContext* MLIRContext = heavy::mlir_helper::getCurrentContext(C);
  mlir::Type BladeType = geomalg::BladeType::get(MLIRContext, Tag);

  C.Cont(C.CreateAny<mlir::Type>(BladeType));
}

// Construct a Blade type from a product of basis vectors
// (which also happen to be given as BladeTypes).
void geomalg_blade_type(heavy::Context& C, heavy::ValueRefs Args) {
  if (Args.size() == 1 && isa<heavy::Int>(Args.front())) {
    // Create the blade using whatever tag value they give us.
    uint32_t Tag = static_cast<uint32_t>(cast<heavy::Int>(Args.front()));
    mlir::MLIRContext* MLIRContext = heavy::mlir_helper::getCurrentContext(C);
    mlir::Type BladeType = geomalg::BladeType::get(MLIRContext, Tag);
    return C.Cont(C.CreateAny<mlir::Type>(BladeType));
  }

  // BladeTypes will consist only of basis 1-blades.
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  for (heavy::Value Arg : Args) {
    mlir::Type Type = any_cast<mlir::Type>(Arg);
    if (auto BladeType = dyn_cast_if_present<geomalg::BladeType>(Type);
        BladeType && BladeType.getGrade() == 1 && BladeType.isCanonical())
      BladeTypes.push_back(BladeType);
    else
      return C.RaiseError(
          "expecting basis vector type (ie grade < 2): {}", Arg);
  }

  if (BladeTypes.empty())
    return C.RaiseError("expecting at least one basis vector type");

  mlir::Type Result = geomalg::createBladeType(BladeTypes);
  heavy::Value AnyVal = C.CreateAny<mlir::Type>(Result);
  return C.Cont(AnyVal);
}

void geomalg_multivector_type(heavy::Context& C, heavy::ValueRefs Args) {
  // Each argument is a possibly improper list of blade types.
  // types used to construct blades.
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  for (heavy::Value List : Args) {
    for (heavy::Value BVArg : List) {
      // Push the positive version of the tag.
      auto Type = heavy::any_cast<mlir::Type>(BVArg);
      if (auto BladeType = dyn_cast_if_present<geomalg::BladeType>(Type))
        BladeTypes.push_back(BladeType.getCanonicalType());
      else
        return C.RaiseError("expecting blade type: {}", BVArg);
    }
  }

  if (BladeTypes.empty())
    return C.RaiseError("multivector type must be nonempty");

  mlir::Type Result = geomalg::createMultivectorType(BladeTypes);
  heavy::Value AnyVal = C.CreateAny<mlir::Type>(Result);
  return C.Cont(AnyVal);
}

// TODO This function is a workaround because we do not have a way
//      to create an operation that implements `inferReturnTypes`.
//      Implement creating operations without result-types when they
//      are not needed (in HeavyScheme.)
// (%sum-impl Loc OpVal1 ... OpValN)
void geomalg_sum_impl(heavy::Context& C, heavy::ValueRefs Args) {
  if (Args.size() < 1)
    return C.RaiseError("invalid arity");

  mlir::MLIRContext* MLIRContext = heavy::mlir_helper::getCurrentContext(C);

  // Loc
  heavy::SourceLocation Loc = Args[0].getSourceLocation();
  // TODO Converting locations should be a function in HeavyScheme.
  mlir::Location MLoc = mlir::OpaqueLoc::get(Loc.getOpaqueEncoding(),
                                             MLIRContext);

  heavy::ValueRefs Operands = Args.drop_front();
  llvm::SmallVector<mlir::Value, 8> Vals;
  for (heavy::Value Operand : Operands) {
    if (mlir::Value V = any_cast<mlir::Value>(Operand))
      Vals.push_back(V);
    else
      return C.RaiseError("expecting mlir.value: {}", Operand);
  }
  mlir::OpBuilder* OpBuilder = heavy::mlir_helper::getCurrentBuilder(C);
  if (!OpBuilder)
    return C.RaiseError("current mlir.builder not set");

  auto SumOp = geomalg::SumOp::create(*OpBuilder, MLoc, Vals);
  heavy::Value Result = C.CreateAny<mlir::Value>(SumOp.getResult());
  C.Cont(Result);
}
}  // extern "C"
