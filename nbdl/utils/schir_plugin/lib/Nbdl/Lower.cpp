// Copyright Jason Rice 2026

#include <nbdl_spec/NbdlDialect.h>
#include <mlir/Conversion/AffineToStandard/AffineToStandard.h>
#include <mlir/Conversion/ArithToLLVM/ArithToLLVM.h>
#include <mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h>
#include <mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h>
#include <mlir/Conversion/LLVMCommon/ConversionTarget.h>
#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h>
#include <mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h>
#include <mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h>
#include <mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h>
#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlow.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/MemRef/Transforms/Transforms.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>

// Generated stuff
namespace nbdl_spec {
#define GEN_PASS_DEF_LOWERPASS
#define GEN_PASS_DEF_LOWERTOLLVMPASS
#include "nbdl_spec/NbdlPasses.h.inc"
}

namespace arith = mlir::arith;
namespace func = mlir::func;
namespace memref = mlir::memref;
namespace scf = mlir::scf;
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

// End an iteration of a loop given the flag to continue and the
// loop-carried values for the next iteration.
void createLoopTerminator(mlir::ConversionPatternRewriter& R,
                          mlir::Operation* Op, mlir::Value Continue,
                          mlir::ValueRange Values) {
  mlir::Location Loc = Op->getLoc();
  if (isa<scf::WhileOp>(Op->getParentOp())) {
    scf::ConditionOp::create(R, Loc, Continue, Values);
    return;
  }
  llvm::SmallVector<mlir::Value, 4> Results{Continue};
  Results.append(Values.begin(), Values.end());
  scf::YieldOp::create(R, Loc, Results);
}

// Create the terminator that replaces a terminating nbdl operation
// that does not continue a loop.
void createTerminator(mlir::ConversionPatternRewriter& R,
                      mlir::Operation* Op) {
  mlir::Location Loc = Op->getLoc();
  if (scf::WhileOp While = getParentLoop(Op)) {
    // Ignore the values for the next iteration.
    mlir::Value False = arith::ConstantIntOp::create(R, Loc, 0, 1);
    createLoopTerminator(R, Op, False, While.getBeforeArguments());
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

    // Within a loop body, yield the flag to continue
    // and the loop-carried values from each branch.
    scf::WhileOp While = getParentLoop(Op);
    llvm::SmallVector<mlir::Type, 4> ResultTs;
    if (While) {
      ResultTs.push_back(R.getI1Type());
      llvm::append_range(ResultTs, While.getBefore().getArgumentTypes());
    }
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
      createLoopTerminator(R, Op, If.getResult(0),
                           If.getResults().drop_front());
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
// iteration arguments. (See getParentLoop.)
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
    llvm::SmallVector<mlir::Location, 4> Locs(Ts.size(), Loc);
    auto While = scf::WhileOp::create(R, Loc, Ts, Inits);

    // Let the terminators of the body end each iteration.
    mlir::Block* Before = R.createBlock(&While.getBefore(), {}, Ts, Locs);
    R.mergeBlocks(*BodyBlock, Before, Before->getArguments());

    mlir::Block* After = R.createBlock(&While.getAfter(), {}, Ts, Locs);
    scf::YieldOp::create(R, Loc, After->getArguments());

    // Terminate the block since nbdl.loop is a terminator,
    // but scf.while is not.
    R.setInsertionPoint(Op);
    createTerminator(R, Op);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Continue the loop with the loop-carried values for the next iteration.
struct LowerContinue : mlir::OpConversionPattern<nbdl_spec::ContinueOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ContinueOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    scf::WhileOp While = getParentLoop(Op);
    if (!While)
      return R.notifyMatchFailure(Op, "loop is not lowered");
    mlir::ValueRange Args = Adaptor.getArgs();
    if (Args.getTypes() != While.getBefore().getArgumentTypes())
      return R.notifyMatchFailure(Op, "loop-carried type mismatch");

    mlir::Value True = arith::ConstantIntOp::create(R, Op.getLoc(), 1, 1);
    createLoopTerminator(R, Op, True, Args);
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
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();

    // Expand memref view operations (e.g. subview) into operations
    // on their strided metadata which MemRefToLLVM can lower.
    {
      mlir::RewritePatternSet PS(Ctx);
      memref::populateExpandStridedMetadataPatterns(PS);
      if (llvm::failed(mlir::applyPatternsGreedily(M, std::move(PS))))
        return signalPassFailure();
    }

    mlir::LLVMTypeConverter TC(Ctx);
    mlir::LLVMConversionTarget Target(*Ctx);

    mlir::RewritePatternSet PS(Ctx);
    mlir::populateAffineToStdConversionPatterns(PS);
    mlir::populateSCFToControlFlowConversionPatterns(PS);
    mlir::arith::populateArithToLLVMConversionPatterns(TC, PS);
    mlir::cf::populateControlFlowToLLVMConversionPatterns(TC, PS);
    mlir::populateFinalizeMemRefToLLVMConversionPatterns(TC, PS);
    mlir::populateFuncToLLVMConversionPatterns(TC, PS);
    mlir::populateVectorToLLVMConversionPatterns(TC, PS);

    if (llvm::failed(mlir::applyPartialConversion(M, Target, std::move(PS))))
      return signalPassFailure();

    // Remove casts between types that are made equivalent.
    llvm::SmallVector<mlir::UnrealizedConversionCastOp> Casts;
    M.walk([&](mlir::UnrealizedConversionCastOp Op) { Casts.push_back(Op); });
    mlir::reconcileUnrealizedCasts(Casts);
  }
};
}  // namespace
