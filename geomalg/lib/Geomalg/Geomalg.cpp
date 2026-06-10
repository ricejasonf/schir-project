#include <geomalg/Dialect.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <mlir/Dialect/Func/Transforms/Passes.h>
#include <mlir/Dialect/Linalg/Passes.h>
#include <mlir/Dialect/PDL/IR/PDL.h>
#include <mlir/Dialect/PDLInterp/IR/PDLInterp.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <schir/Context.h>
#include <schir/MlirHelper.h>
#include <schir/Value.h>

namespace {
template <typename T>
void CreateMultivectorLikeType(schir::Context& C, schir::ValueRefs Args) {
  // Each argument is a possibly improper list of blade types.
  // types used to construct blades.
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  for (schir::Value List : Args) {
    for (schir::Value BVArg : List) {
      // Push the positive version of the tag.
      auto Type = schir::any_cast<mlir::Type>(BVArg);
      if (auto BladeType = dyn_cast_if_present<geomalg::BladeType>(Type))
        BladeTypes.push_back(BladeType.getCanonicalType());
      else
        return C.RaiseError("expecting blade type: {}", BVArg);
    }
  }

  if (BladeTypes.empty())
    return C.RaiseError("multivector type must be nonempty");

  mlir::Type Result = geomalg::createMultivectorLikeType<T>(BladeTypes);
  schir::Value AnyVal = C.CreateAny<mlir::Type>(Result);
  return C.Cont(AnyVal);
}

} // namespace

extern "C" {
schir::ContextLocal geomalg_current_module;

void geomalg_init(schir::Context& C, schir::ValueRefs) {
  C.DialectRegistry->insert<geomalg::GeomalgDialect>();
  C.DialectRegistry->insert<mlir::func::FuncDialect>();
  C.DialectRegistry->insert<mlir::spirv::SPIRVDialect>();
  C.DialectRegistry->insert<mlir::vector::VectorDialect>();
  //C.DialectRegistry->insert<mlir::pdl::PDLDialect>();
  //C.DialectRegistry->insert<mlir::pdl_interp::PDLInterpDialect>();

  geomalg::registerGeomalgPasses();
  geomalg::registerGeomalgToSPIRV();
  geomalg::registerGeomalgToLLVM();
  C.Cont();
}

void geomalg_basis_vector_type(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (!isa<schir::Int>(Args[0]))
    return C.RaiseError("expecting integer: {}", Args[0]);

  uint32_t Tag = static_cast<uint32_t>(cast<schir::Int>(Args[0]));

  // A basis vector must be a power of 2 that is
  // not highest representable power of 2.
  // This also means it is does not contain a wedge
  // product of two nontrivial vectors.
  if (std::popcount(Tag) > 1)
    return C.RaiseError("expecting power of two: {}", Args[0]);

  mlir::MLIRContext* MLIRContext = schir::mlir_helper::getCurrentContext(C);
  mlir::Type BladeType = geomalg::BladeType::get(MLIRContext, Tag);

  C.Cont(C.CreateAny<mlir::Type>(BladeType));
}

// Construct a Blade type from a product of basis vectors
// (which also happen to be given as BladeTypes).
void geomalg_blade_type(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() == 1 && isa<schir::Int>(Args.front())) {
    // Create the blade using whatever tag value they give us.
    uint32_t Tag = static_cast<uint32_t>(cast<schir::Int>(Args.front()));
    mlir::MLIRContext* MLIRContext = schir::mlir_helper::getCurrentContext(C);
    mlir::Type BladeType = geomalg::BladeType::get(MLIRContext, Tag);
    return C.Cont(C.CreateAny<mlir::Type>(BladeType));
  }

  // BladeTypes will consist only of basis 1-blades.
  llvm::SmallVector<geomalg::BladeType, 8> BladeTypes;
  for (schir::Value Arg : Args) {
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
  schir::Value AnyVal = C.CreateAny<mlir::Type>(Result);
  return C.Cont(AnyVal);
}

void geomalg_multivector_type(schir::Context& C, schir::ValueRefs Args) {
  CreateMultivectorLikeType<geomalg::MultivectorType>(C, Args);
}

void geomalg_unitvector_type(schir::Context& C, schir::ValueRefs Args) {
  CreateMultivectorLikeType<geomalg::UnitVectorType>(C, Args);
}

}  // extern "C"
