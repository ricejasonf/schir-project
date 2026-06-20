#include <geomalg/Dialect.h>
#include <geomalg/Metric.h>
#include <geomalg/Passes.h>
#include <geomalg/Type.h>
#include <mlir/Dialect/Func/Transforms/Passes.h>
#include <mlir/Dialect/Linalg/Passes.h>
#include <mlir/Dialect/PDL/IR/PDL.h>
#include <mlir/Dialect/PDLInterp/IR/PDLInterp.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/Pass/PassManager.h>
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
schir::ContextLocal geomalg_current_metric;

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

// Set the current metric
void geomalg_with_metric(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  schir::Value Arg = Args.front();
  if (!isa<schir::Int>(Arg))
    return C.RaiseError("expecting int: {}", Arg);
  geomalg_current_metric.set(C, Arg);
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

// Run ExpandFuncPass with the current metric if it is known.
// Update the function return type now so we have it for possible
// introspection.
void geomalg_apply_metric(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args.front());
  if (!Op)
    return C.RaiseError("expecting mlir.operation: {}", Args.front());

  auto FuncOp = dyn_cast<mlir::FunctionOpInterface>(Op);
  if (!FuncOp)
    return C.RaiseError("expecting mlir.op of mlir::FunctionOpInterface: {}",
                        Args.front());

  // Do nothing if the metric is unknown.
  geomalg::MetricKind MetricKind = geomalg::getCurrentMetric(C);
  if (MetricKind == geomalg::MetricKind::unknown)
    return C.Cont();

  geomalg::ExpandFuncPassOptions ExpandPassOpts{
    .metric = MetricKind
  };

  mlir::MLIRContext* MCtx = schir::mlir_helper::getCurrentContext(C);
  if (!MCtx)
    return C.RaiseError("mlir context not set");

  // Run ExpandFuncPass on the Op.
  mlir::PassManager PM(MCtx);
  PM.addPass(geomalg::createExpandFuncPass(ExpandPassOpts));

  if (schir::isPassDebugMode(C)) {
#ifndef NDEBUG
    llvm::DebugFlag = true;
#endif
    MCtx->disableMultithreading();
    PM.enableIRPrinting();
  }

  // TODO The ScopedDiagnosticHandler stuff should have a helper
  //      in MlirHelpers or something.
  //      This code is redundant with (schir mlir all-passes).

  // Attach mlir diagnostics as "notes" to the scheme error
  // to be raised if PassManager::run fails.
  llvm::SmallVector<schir::Value, 1> Irrs;
  mlir::ScopedDiagnosticHandler DH(MCtx,
      [&](mlir::Diagnostic& D) -> llvm::LogicalResult {
        std::string ErrMsg = D.str();
        mlir::Location ErrLoc = D.getLocation();
        auto Loc = schir::SourceLocation(mlir::OpaqueLoc
          ::getUnderlyingLocationOrNull<
              schir::SourceLocationEncoding*>(ErrLoc));
        schir::Value Error = C.CreateError(Loc, llvm::StringRef(ErrMsg),
                                           schir::Empty());
        Irrs.push_back(Error);
        return llvm::failure();
      });

  if (mlir::failed(PM.run(Op)))
    return C.RaiseError("failed to apply metric to function: {}", Irrs);

  // Update the function return type.
  auto FT = dyn_cast<mlir::FunctionType>(FuncOp.getFunctionType());
  if (!FT || FT.getNumResults() != 1)
    return C.RaiseError("func does not have valid function type: {}",
                        schir::Value(Op));

  mlir::Type ResultT = FT.getResult(0);
  mlir::Operation* ReturnOp = FuncOp.getFunctionBody().front().getTerminator();
  mlir::Type NewResultT = ReturnOp->getOperand(0).getType();
  if (ResultT != NewResultT) {
    mlir::FunctionType NewFT = mlir::FunctionType::get(
        MCtx, FT.getInputs(), NewResultT);
    FuncOp.setType(NewFT);
  }

  C.Cont();
}

}  // extern "C"

geomalg::MetricKind geomalg::getCurrentMetric(schir::Context& C) {
  schir::Value V = geomalg_current_metric.get(C);

  if (!isa<schir::Int>(V))
    return geomalg::MetricKind::unknown;

  auto Int = cast<schir::Int>(V);
  return static_cast<geomalg::MetricKind>(static_cast<int32_t>(Int));
}

// Not used
void geomalg::setCurrentMetric(schir::Context& C, geomalg::MetricKind MK) {
  schir::Int Int(static_cast<int32_t>(MK));
  geomalg_current_metric.set(C, schir::Value(Int));
}
