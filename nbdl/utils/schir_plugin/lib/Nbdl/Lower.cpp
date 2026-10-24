// Copyright Jason Rice 2026

#include <nbdl_spec/NbdlDialect.h>
#include <schir_common/Lower.h>
#include <schir_common/SPIRVConversion.h>
#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlow.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVOps.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVTypes.h>
#include <mlir/Dialect/SPIRV/IR/TargetAndABI.h>
#include <mlir/Dialect/SPIRV/Transforms/Passes.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/Twine.h>
#include <optional>
#include <string>

// Generated stuff
namespace nbdl_spec {
#define GEN_PASS_DEF_LOWERPASS
#define GEN_PASS_DEF_LOWERTOLLVMPASS
#define GEN_PASS_DEF_LOWERTOSPIRVPASS
#include "nbdl_spec/NbdlPasses.h.inc"
}

namespace arith = mlir::arith;
namespace func = mlir::func;
namespace memref = mlir::memref;
namespace scf = mlir::scf;
namespace spirv = mlir::spirv;
namespace vector = mlir::vector;
using llvm::dyn_cast;
using llvm::isa;

namespace {
// A store with a single alternative is converted to that contained type.
struct TypeConverter : mlir::TypeConverter {
  TypeConverter()
    : mlir::TypeConverter()
  {
    addConversion([](mlir::Type T) { return T; });
    // Returning a null type indicates failure.
    addConversion([](nbdl_spec::StoreType ST) -> mlir::Type {
      if (ST.getAlts().size() != 1)
        return mlir::Type();
      mlir::Type AltT = ST.getAlts().front().getValue();
      if (isa<nbdl_spec::CppType>(AltT))
        return mlir::Type();
      return AltT;
    });
  }
};

// Function signatures are not converted since define-fn uses raw types.
struct ConversionTarget : mlir::ConversionTarget {
  ConversionTarget(mlir::MLIRContext& Ctx)
    : mlir::ConversionTarget(Ctx)
  {
    addLegalDialect<arith::ArithDialect>();
    addLegalDialect<func::FuncDialect>();
    addLegalDialect<memref::MemRefDialect>();
    addLegalDialect<scf::SCFDialect>();
    addIllegalDialect<nbdl_spec::NbdlDialect>();
    // These are erased after the conversion when they have no uses.
    addLegalOp<nbdl_spec::UnitOp, nbdl_spec::FuncNameOp>();
  }
};

// Lower a loop to a scf.while whose before region is the body.
// End each path through the body by yielding a flag to continue and
// the loop-carried values for the next iteration via the scf.if of
// each lowered match_if.
//  scf.while (%arg = %init) : (T) -> T {
//    %continue, %next = scf.if %cond -> (i1, T) {
//      ...
//      scf.yield %false, %arg : i1, T  // exit and ignore the values
//    } else {
//      ...
//      scf.yield %true, %value : i1, T // continue with the values
//    }
//    scf.condition(%continue) %next : T
//  } do {
//  ^bb0(%next: T):
//    scf.yield %next : T
//  }
//
// Let a loop within the body of a parent loop also yield the flag and
// values of the parent loop as results so its terminators may continue
// the parent loop (ie the iteration values of every parent loop.)
//  scf.while (%arg = %init) : (T) -> (T, i1, U) {
//    ...
//    scf.condition(%false) %arg, %true, %value : T, i1, U
//  } do { ... }
//
// Get the scf.while of the loop that the terminator of the block of Op
// ends an iteration of or a null op if it is not within a loop body.
// Walk up through the scf.if (with results) of each lowered match_if.
// (Rely on lowering converting parents before the operations in their
// regions.)
scf::WhileOp getParentLoop(mlir::Operation* Op) {
  mlir::Region* Region = Op->getParentRegion();
  while (auto If = dyn_cast<scf::IfOp>(Region->getParentOp())) {
    if (If.getNumResults() == 0)
      return {};
    Region = If->getParentRegion();
  }
  auto While = dyn_cast<scf::WhileOp>(Region->getParentOp());
  if (!While || Region != &While.getBefore())
    return {};
  return While;
}

// The attribute that marks the function of the loop of a scf.while
// until the lowering is complete.
constexpr llvm::StringLiteral LoopFnAttrName = "nbdl.loop_fn";

// Get the types of the values that end an iteration of a loop
// (ie the flag to continue and the results of the scf.while.)
llvm::SmallVector<mlir::Type, 4> getIterationTypes(scf::WhileOp While) {
  llvm::SmallVector<mlir::Type, 4> Ts{mlir::IntegerType::get(
                                        While.getContext(), 1)};
  llvm::append_range(Ts, While.getResultTypes());
  return Ts;
}

// Append the values that end an iteration of a loop and continue the
// loop Target (if any) with Args or exit every other loop.
// Pass the current values to a loop that does not continue
// since they are ignored.
void appendIterationValues(mlir::ConversionPatternRewriter& R,
                           mlir::Location Loc, scf::WhileOp While,
                           scf::WhileOp Target, mlir::ValueRange Args,
                           llvm::SmallVectorImpl<mlir::Value>& Values) {
  bool IsTarget = While == Target;
  Values.push_back(arith::ConstantIntOp::create(R, Loc, IsTarget, 1));
  if (IsTarget)
    llvm::append_range(Values, Args);
  else
    llvm::append_range(Values, While.getBeforeArguments());
  if (scf::WhileOp Parent = getParentLoop(While))
    appendIterationValues(R, Loc, Parent, IsTarget ? scf::WhileOp() : Target,
                          Args, Values);
}

// End an iteration of a loop given the flag to continue and
// the results of the scf.while.
void createLoopTerminator(mlir::ConversionPatternRewriter& R,
                          mlir::Operation* Op, mlir::ValueRange Values) {
  mlir::Location Loc = Op->getLoc();
  if (isa<scf::WhileOp>(Op->getParentOp())) {
    scf::ConditionOp::create(R, Loc, Values.front(), Values.drop_front());
    return;
  }
  scf::YieldOp::create(R, Loc, Values);
}

// Create the terminator that replaces a terminating nbdl operation
// that does not continue a loop.
void createTerminator(mlir::ConversionPatternRewriter& R,
                      mlir::Operation* Op) {
  mlir::Location Loc = Op->getLoc();
  if (scf::WhileOp While = getParentLoop(Op)) {
    // Exit every loop.
    llvm::SmallVector<mlir::Value, 4> Values;
    appendIterationValues(R, Loc, While, scf::WhileOp(), {}, Values);
    createLoopTerminator(R, Op, Values);
  } else if (isa<func::FuncOp>(Op->getParentOp())) {
    func::ReturnOp::create(R, Loc);
  } else {
    scf::YieldOp::create(R, Loc);
  }
}

// Conversion Patterns

struct LowerReturn : mlir::OpConversionPattern<nbdl_spec::ReturnOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ReturnOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.replaceOpWithNewOp<func::ReturnOp>(Op, Adaptor.getOperands());
    return llvm::success();
  }
};

// Replace terminators that discard a value with a terminator
// for the parent operation.
template <typename OpTy>
struct LowerDiscardLike : mlir::OpConversionPattern<OpTy> {
  using mlir::OpConversionPattern<OpTy>::OpConversionPattern;

  llvm::LogicalResult matchAndRewrite(
        OpTy Op, typename OpTy::Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.setInsertionPoint(Op);
    createTerminator(R, Op);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Unwrapping or lifting a store is the identity on converted values
// since a store is converted to its contained type.
template <typename OpTy>
struct LowerStoreCast : mlir::OpConversionPattern<OpTy> {
  using mlir::OpConversionPattern<OpTy>::OpConversionPattern;

  llvm::LogicalResult matchAndRewrite(
        OpTy Op, typename OpTy::Adaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Value Value = Adaptor.getValue();
    mlir::Type ResultT =
      this->getTypeConverter()->convertType(Op.getResult().getType());
    if (Value.getType() != ResultT)
      return R.notifyMatchFailure(Op, "store cast type mismatch");
    R.replaceOp(Op, Value);
    return llvm::success();
  }
};

struct LowerLiteral : mlir::OpConversionPattern<nbdl_spec::LiteralOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::LiteralOp Op, OpAdaptor,
        mlir::ConversionPatternRewriter& R) const override {
    // The literal is not a store so it needs no conversion.
    auto Value = dyn_cast<mlir::TypedAttr>(Op.getValue());
    if (!Value || Value.getType() != Op.getType())
      return R.notifyMatchFailure(Op, "literal is not an arith constant");
    R.replaceOpWithNewOp<arith::ConstantOp>(Op, Value);
    return llvm::success();
  }
};

// Lower a visit on a function in the module to func.call.
struct LowerVisit : mlir::OpConversionPattern<nbdl_spec::VisitOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::VisitOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    if (Op.getSfinae() || Op.getValidCppCrossMap())
      return R.notifyMatchFailure(Op, "visit requires C++");
    func::FuncOp Callee = Op.lookupCallee();
    if (!Callee)
      return R.notifyMatchFailure(Op, "callee is not a func in the module");

    // The callee has raw types that require no conversion.
    mlir::TypeRange ResultTs = Callee.getResultTypes();
    if (ResultTs.size() > 1)
      return R.notifyMatchFailure(Op, "callee has multiple results");

    auto Call = func::CallOp::create(R, Op.getLoc(), Callee.getSymName(),
                                     ResultTs, Adaptor.getArgs());
    // The result of a call may be discarded.
    if (ResultTs.empty() || isa<nbdl_spec::UnitType>(Op.getType()))
      R.replaceOpWithNewOp<nbdl_spec::UnitOp>(Op,
          nbdl_spec::UnitType::get(Op.getContext()));
    else
      R.replaceOp(Op, Call.getResults());
    return llvm::success();
  }
};

struct LowerMatchIf : mlir::OpConversionPattern<nbdl_spec::MatchIfOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::MatchIfOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Value Cond = Adaptor.getCond();
    if (!Cond.getType().isInteger(1))
      return R.notifyMatchFailure(Op, "condition is not i1");
    mlir::Block& ThenBlock = Op.getThenRegion().front();
    mlir::Block& ElseBlock = Op.getElseRegion().front();
    if (ThenBlock.getNumArguments() != 1 || ElseBlock.getNumArguments() != 0)
      return R.notifyMatchFailure(Op, "unexpected region arguments");

    // Within a loop body, yield the values that
    // end the iteration from each branch.
    scf::WhileOp While = getParentLoop(Op);
    llvm::SmallVector<mlir::Type, 4> ResultTs;
    if (While)
      ResultTs = getIterationTypes(While);
    auto If = scf::IfOp::create(R, Op.getLoc(), ResultTs, Cond,
                                /*addThenBlock=*/false,
                                /*addElseBlock=*/false);
    // The argument of the then region is the condition itself.
    mlir::Block* NewThen = R.createBlock(&If.getThenRegion());
    R.mergeBlocks(&ThenBlock, NewThen, Cond);
    mlir::Block* NewElse = R.createBlock(&If.getElseRegion());
    R.mergeBlocks(&ElseBlock, NewElse, mlir::ValueRange{});

    // nbdl.match_if is a terminator, but scf.if is not.
    R.setInsertionPointAfter(If);
    if (While)
      createLoopTerminator(R, Op, If.getResults());
    else
      createTerminator(R, Op);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Lower the scope to a region with the same terminator semantics.
struct LowerScope : mlir::OpConversionPattern<nbdl_spec::ScopeOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ScopeOp Op, OpAdaptor,
        mlir::ConversionPatternRewriter& R) const override {
    auto ExecOp = scf::ExecuteRegionOp::create(R, Op.getLoc(),
                                               mlir::TypeRange{});
    R.inlineRegionBefore(Op.getBody(), ExecOp.getRegion(),
                         ExecOp.getRegion().end());
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Lower match_each on a memref to nested loops that
// load each element in row-major order.
struct LowerMatchEach : mlir::OpConversionPattern<nbdl_spec::MatchEachOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::MatchEachOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Value Range = Adaptor.getRange();
    auto MRT = dyn_cast<mlir::MemRefType>(Range.getType());
    if (!MRT)
      return R.notifyMatchFailure(Op, "range is not a memref");
    if (MRT.getRank() == 0)
      return R.notifyMatchFailure(Op, "memref has rank 0");
    mlir::Region& Body = Op.getBody();
    if (Body.getNumArguments() != 1)
      return R.notifyMatchFailure(Op, "unexpected region arguments");
    mlir::Type ElementT =
      getTypeConverter()->convertType(Body.getArgument(0).getType());
    if (ElementT != MRT.getElementType())
      return R.notifyMatchFailure(Op, "element type mismatch");
    llvm::FailureOr<mlir::Block*> BodyBlock =
      R.convertRegionTypes(&Body, *getTypeConverter());
    if (llvm::failed(BodyBlock))
      return R.notifyMatchFailure(Op, "region type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::Value Zero = arith::ConstantIndexOp::create(R, Loc, 0);
    mlir::Value One = arith::ConstantIndexOp::create(R, Loc, 1);
    llvm::SmallVector<mlir::Value, 4> Indices;
    scf::ForOp InnerFor;
    for (int64_t I = 0; I < MRT.getRank(); ++I) {
      mlir::Value Dim = memref::DimOp::create(R, Loc, Range, I);
      InnerFor = scf::ForOp::create(R, Loc, Zero, Dim, One);
      Indices.push_back(InnerFor.getInductionVar());
      R.setInsertionPointToStart(InnerFor.getBody());
    }

    // The body has its own terminator that replaces the yield.
    mlir::Block* InnerBody = InnerFor.getBody();
    R.eraseOp(InnerBody->getTerminator());
    R.setInsertionPointToEnd(InnerBody);
    mlir::Value Element = memref::LoadOp::create(R, Loc, Range, Indices);
    R.mergeBlocks(*BodyBlock, InnerBody, Element);

    // nbdl.match_each is a terminator, but scf.for is not.
    R.setInsertionPoint(Op);
    createTerminator(R, Op);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Lower a loop to a scf.while with the loop-carried values as its
// iteration arguments. Within a parent loop, end the iteration of the
// parent loop with the results that follow the loop-carried values.
// (See getParentLoop.)
struct LowerLoop : mlir::OpConversionPattern<nbdl_spec::LoopOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::LoopOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    llvm::FailureOr<mlir::Block*> BodyBlock =
      R.convertRegionTypes(&Op.getBody(), *getTypeConverter());
    if (llvm::failed(BodyBlock))
      return R.notifyMatchFailure(Op, "region type conversion failed");

    mlir::Location Loc = Op.getLoc();
    mlir::ValueRange Inits = Adaptor.getInits();
    mlir::TypeRange Ts = Inits.getTypes();
    scf::WhileOp Parent = getParentLoop(Op);
    llvm::SmallVector<mlir::Type, 4> ResultTs(Ts);
    if (Parent)
      llvm::append_range(ResultTs, getIterationTypes(Parent));
    auto While = scf::WhileOp::create(R, Loc, ResultTs, Inits);
    While->setAttr(LoopFnAttrName, Op.getFnAttr());

    // Let the terminators of the body end each iteration.
    llvm::SmallVector<mlir::Location, 4> Locs(ResultTs.size(), Loc);
    mlir::Block* Before = R.createBlock(&While.getBefore(), {}, Ts,
                                        llvm::ArrayRef(Locs).take_front(
                                          Ts.size()));
    R.mergeBlocks(*BodyBlock, Before, Before->getArguments());

    mlir::Block* After = R.createBlock(&While.getAfter(), {}, ResultTs,
                                       Locs);
    scf::YieldOp::create(R, Loc, After->getArguments().take_front(
                                   Ts.size()));

    // Terminate the block since nbdl.loop is a terminator,
    // but scf.while is not.
    R.setInsertionPoint(Op);
    if (Parent)
      createLoopTerminator(R, Op, While.getResults().drop_front(Ts.size()));
    else
      createTerminator(R, Op);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Continue the loop with the loop-carried values for the next iteration
// and exit each loop within it.
struct LowerContinue : mlir::OpConversionPattern<nbdl_spec::ContinueOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ContinueOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    scf::WhileOp While = getParentLoop(Op);
    scf::WhileOp Target = While;
    while (Target && Target->getAttr(LoopFnAttrName) != Op.getFnAttr())
      Target = getParentLoop(Target);
    if (!Target)
      return R.notifyMatchFailure(Op, "loop is not lowered");
    mlir::ValueRange Args = Adaptor.getArgs();
    if (Args.getTypes() != Target.getBefore().getArgumentTypes())
      return R.notifyMatchFailure(Op, "loop-carried type mismatch");

    llvm::SmallVector<mlir::Value, 4> Values;
    appendIterationValues(R, Op.getLoc(), While, Target, Args, Values);
    createLoopTerminator(R, Op, Values);
    R.eraseOp(Op);
    return llvm::success();
  }
};

void populateLowerPatterns(mlir::RewritePatternSet& PS,
                           ::TypeConverter const& TC) {
  mlir::MLIRContext* Ctx = PS.getContext();
  PS.add<LowerReturn,
         LowerDiscardLike<nbdl_spec::DiscardOp>,
         LowerDiscardLike<nbdl_spec::NoOp>,
         LowerStoreCast<nbdl_spec::UnwrapOp>,
         LowerStoreCast<nbdl_spec::LiftStoreOp>,
         LowerLiteral,
         LowerVisit,
         LowerMatchIf,
         LowerMatchEach,
         LowerScope,
         LowerLoop,
         LowerContinue
         >(TC, Ctx);
}

// Lowering Passes

class LowerPass : public nbdl_spec::impl::LowerPassBase<LowerPass> {
  using Base = nbdl_spec::impl::LowerPassBase<LowerPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();

    ::TypeConverter TC;
    ::ConversionTarget Target(*Ctx);
    mlir::RewritePatternSet PS(Ctx);
    populateLowerPatterns(PS, TC);

    mlir::ConversionConfig Config;
    Config.allowPatternRollback = false;
    if (llvm::failed(mlir::applyPartialConversion(M, Target, std::move(PS),
                                                  Config)))
      return signalPassFailure();

    // Erase the remaining operations that were only used by nbdl operations.
    M.walk([](mlir::Operation* Op) {
      if (isa<nbdl_spec::UnitOp, nbdl_spec::FuncNameOp>(Op) && Op->use_empty())
        Op->erase();
      else if (isa<scf::WhileOp>(Op))
        Op->removeAttr(LoopFnAttrName);
    });
  }
};

// Lower the mid level dialects that nbdl-lower produces to LLVM.
class LowerToLLVMPass
    : public nbdl_spec::impl::LowerToLLVMPassBase<LowerToLLVMPass> {
  using Base = nbdl_spec::impl::LowerToLLVMPassBase<LowerToLLVMPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    if (llvm::failed(schir_common::lowerToLLVM(getOperation())))
      signalPassFailure();
  }
};

// The entry point of a shader to declare once its function is lowered.
struct ShaderEntry {
  std::string Name;
  spirv::ExecutionModel Model;
  llvm::SmallVector<int32_t, 3> LocalSize;
  // The Input and Output variables.
  llvm::SmallVector<mlir::Attribute, 4> InterfaceVars;
};

// Create the variable of a parameter or result of type T of a shader entry
// described by a dictionary of its storage class and decorations.
spirv::GlobalVariableOp createInterfaceVar(mlir::OpBuilder& B,
                                           func::FuncOp F,
                                           llvm::Twine const& Name,
                                           mlir::Type T,
                                           mlir::DictionaryAttr Desc,
                                           mlir::TypeConverter const& TC) {
  std::optional<spirv::StorageClass> SC;
  if (Desc)
    if (auto SCName = Desc.getAs<mlir::StringAttr>("storage_class"))
      SC = spirv::symbolizeStorageClass(SCName.getValue());
  if (!SC) {
    F.emitError("invalid shader interface variable: ") << Desc;
    return {};
  }
  // Pass only scalars and vectors that are the same type in SPIRV.
  if (!isa<mlir::IntegerType, mlir::FloatType, mlir::VectorType>(T) ||
      TC.convertType(T) != T) {
    F.emitError("unsupported shader interface type: ") << T;
    return {};
  }
  // Wrap a buffer in a struct as Vulkan requires.
  mlir::Type PointeeT = T;
  if (*SC == spirv::StorageClass::Uniform)
    PointeeT = spirv::StructType::get({T}, {0});

  auto PtrT = spirv::PointerType::get(PointeeT, *SC);
  auto Var = spirv::GlobalVariableOp::create(
      B, F.getLoc(), mlir::TypeAttr::get(PtrT), B.getStringAttr(Name));
  if (auto A = Desc.getAs<mlir::IntegerAttr>("location"))
    Var.setLocationAttr(A);
  if (auto A = Desc.getAs<mlir::StringAttr>("built_in"))
    Var.setBuiltInAttr(A);
  if (auto A = Desc.getAs<mlir::IntegerAttr>("descriptor_set"))
    Var.setDescriptorSetAttr(A);
  if (auto A = Desc.getAs<mlir::IntegerAttr>("binding"))
    Var.setBindingAttr(A);
  return Var;
}

// Load the value of a parameter of a shader entry from its variable.
mlir::Value loadInterfaceVar(mlir::OpBuilder& B, mlir::Location Loc,
                             spirv::GlobalVariableOp Var) {
  mlir::Value Ptr = spirv::AddressOfOp::create(B, Loc, Var);
  auto PtrT = cast<spirv::PointerType>(Var.getType());
  if (isa<spirv::StructType>(PtrT.getPointeeType())) {
    mlir::Value Zero = spirv::ConstantOp::getZero(B.getI32Type(), Loc, B);
    Ptr = spirv::AccessChainOp::create(B, Loc, Ptr, Zero);
  }
  return spirv::LoadOp::create(B, Loc, Ptr);
}

// Turn a function into the void() function of a shader entry point that
// loads its parameters from and stores its results to global variables.
llvm::FailureOr<ShaderEntry> createShaderEntry(func::FuncOp F,
                                               mlir::DictionaryAttr Desc,
                                               mlir::TypeConverter const& TC) {
  auto Name = Desc.getAs<mlir::StringAttr>("name");
  auto ModelName = Desc.getAs<mlir::StringAttr>("execution_model");
  auto InputDescs = Desc.getAs<mlir::ArrayAttr>("inputs");
  auto OutputDescs = Desc.getAs<mlir::ArrayAttr>("outputs");
  std::optional<spirv::ExecutionModel> Model;
  if (ModelName)
    Model = spirv::symbolizeExecutionModel(ModelName.getValue());
  mlir::FunctionType FT = F.getFunctionType();
  if (!Name || !Model || !InputDescs || !OutputDescs || F.isExternal() ||
      InputDescs.size() != FT.getNumInputs() ||
      OutputDescs.size() != FT.getNumResults())
    return F.emitError("invalid shader entry: ") << Desc;

  ShaderEntry Entry{Name.str(), *Model, {}, {}};
  if (auto LocalSize = Desc.getAs<mlir::DenseI32ArrayAttr>("local_size"))
    llvm::append_range(Entry.LocalSize, LocalSize.asArrayRef());

  // Create the variables before the function.
  mlir::OpBuilder B(F);
  auto CreateVars = [&](mlir::ArrayAttr Descs, mlir::TypeRange Ts,
                        llvm::StringRef Suffix,
                        llvm::SmallVectorImpl<spirv::GlobalVariableOp>& Vars) {
    for (auto [I, VarDesc] : llvm::enumerate(Descs)) {
      spirv::GlobalVariableOp Var = createInterfaceVar(
          B, F, Name.getValue() + Suffix + llvm::Twine(I), Ts[I],
          dyn_cast<mlir::DictionaryAttr>(VarDesc), TC);
      if (!Var)
        return llvm::failure();
      spirv::StorageClass SC =
        cast<spirv::PointerType>(Var.getType()).getStorageClass();
      if (SC == spirv::StorageClass::Input ||
          SC == spirv::StorageClass::Output)
        Entry.InterfaceVars.push_back(
          mlir::FlatSymbolRefAttr::get(Var.getSymNameAttr()));
      Vars.push_back(Var);
    }
    return llvm::success();
  };
  llvm::SmallVector<spirv::GlobalVariableOp, 4> InputVars, OutputVars;
  if (llvm::failed(CreateVars(InputDescs, FT.getInputs(), "_arg",
                              InputVars)) ||
      llvm::failed(CreateVars(OutputDescs, FT.getResults(), "_result",
                              OutputVars)))
    return llvm::failure();

  // Load the parameters at the beginning of the body.
  mlir::Block& Body = F.front();
  B.setInsertionPointToStart(&Body);
  for (auto [Arg, Var] : llvm::zip_equal(Body.getArguments(), InputVars))
    Arg.replaceAllUsesWith(loadInterfaceVar(B, F.getLoc(), Var));
  Body.eraseArguments(0, Body.getNumArguments());

  // Store the results at each return.
  llvm::SmallVector<func::ReturnOp, 4> Returns;
  F.walk([&](func::ReturnOp Ret) { Returns.push_back(Ret); });
  for (func::ReturnOp Ret : Returns) {
    B.setInsertionPoint(Ret);
    for (auto [Result, Var] : llvm::zip_equal(Ret.getOperands(), OutputVars)) {
      mlir::Value Ptr = spirv::AddressOfOp::create(B, Ret.getLoc(), Var);
      spirv::StoreOp::create(B, Ret.getLoc(), Ptr, Result);
    }
    Ret->eraseOperands(0, Ret->getNumOperands());
  }

  F.setFunctionType(B.getFunctionType({}, {}));
  F.setSymName(Name.getValue());
  F->removeAttr(nbdl_spec::ShaderEntryAttrName);
  return Entry;
}

// Declare the entry point of a lowered function with the execution modes
// that its execution model requires.
void createEntryPoint(mlir::OpBuilder& B, spirv::ModuleOp SPIRVModule,
                      ShaderEntry const& Entry) {
  auto Fn = SPIRVModule.lookupSymbol<spirv::FuncOp>(Entry.Name);
  assert(Fn && "expecting the lowered function of the entry point");
  mlir::Location Loc = Fn.getLoc();
  spirv::EntryPointOp::create(B, Loc, Entry.Model, Fn, Entry.InterfaceVars);
  if (Entry.Model == spirv::ExecutionModel::Fragment)
    spirv::ExecutionModeOp::create(B, Loc, Fn,
                                   spirv::ExecutionMode::OriginUpperLeft, {});
  if (!Entry.LocalSize.empty())
    spirv::ExecutionModeOp::create(B, Loc, Fn, spirv::ExecutionMode::LocalSize,
                                   Entry.LocalSize);
}

// Lower the mid level dialects that nbdl-lower produces to SPIRV.
class LowerToSPIRVPass
    : public nbdl_spec::impl::LowerToSPIRVPassBase<LowerToSPIRVPass> {
  using Base = nbdl_spec::impl::LowerToSPIRVPassBase<LowerToSPIRVPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::ModuleOp M = getOperation();
    spirv::TargetEnvAttr TEA = getVulkanTargetEnv();
    M->setAttr(spirv::getTargetEnvAttrName(), TEA);

    // Turn the marked functions into entry point functions.
    schir_common::SPIRVTypeConverter TC(TEA);
    llvm::SmallVector<ShaderEntry, 4> Entries;
    for (func::FuncOp F :
         llvm::make_early_inc_range(M.getOps<func::FuncOp>())) {
      auto Desc = F->getAttrOfType<mlir::DictionaryAttr>(
          nbdl_spec::ShaderEntryAttrName);
      if (!Desc)
        continue;
      llvm::FailureOr<ShaderEntry> Entry = createShaderEntry(F, Desc, TC);
      if (llvm::failed(Entry))
        return signalPassFailure();
      Entries.push_back(std::move(*Entry));
    }

    if (llvm::failed(schir_common::lowerToSPIRV(M)))
      return signalPassFailure();

    // Move the lowered functions into a spirv.module.
    mlir::OpBuilder B = mlir::OpBuilder::atBlockEnd(M.getBody());
    auto SPIRVModule = spirv::ModuleOp::create(B, M.getLoc(),
                                               spirv::AddressingModel::Logical,
                                               spirv::MemoryModel::GLSL450);
    mlir::Block* Body = SPIRVModule.getBody();
    for (mlir::Operation& Op : llvm::make_early_inc_range(*M.getBody()))
      if (&Op != SPIRVModule)
        Op.moveBefore(Body, Body->end());

    B.setInsertionPointToEnd(Body);
    for (ShaderEntry const& Entry : Entries)
      createEntryPoint(B, SPIRVModule, Entry);

    // Require the minimal version, capabilities and extensions.
    mlir::OpPassManager PM(spirv::ModuleOp::getOperationName());
    PM.addPass(spirv::createSPIRVUpdateVCEPass());
    if (llvm::failed(runPipeline(PM, SPIRVModule)))
      signalPassFailure();
  }

private:
  // Target SPIRV 1.0 with Vulkan to support any Vulkan version.
  spirv::TargetEnvAttr getVulkanTargetEnv() {
    mlir::MLIRContext* Ctx = &getContext();
    auto VCE = spirv::VerCapExtAttr::get(
        spirv::Version::V_1_0, {spirv::Capability::Shader},
        {spirv::Extension::SPV_KHR_storage_buffer_storage_class}, Ctx);
    return spirv::TargetEnvAttr::get(
        VCE, spirv::getDefaultResourceLimits(Ctx), spirv::ClientAPI::Vulkan,
        spirv::Vendor::Unknown, spirv::DeviceType::Unknown,
        spirv::TargetEnvAttr::kUnknownDeviceID);
  }
};
}  // namespace
