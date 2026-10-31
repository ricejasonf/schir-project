// Copyright Jason Rice 2026

#include <nbdl_spec/NbdlDialect.h>
#include <schir_common/Lower.h>
#include <schir_common/SPIRVConversion.h>
#include <mlir/Conversion/LLVMCommon/Pattern.h>
#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Conversion/MemRefToSPIRV/MemRefToSPIRV.h>
#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlow.h>
#include <mlir/Dialect/Func/Extensions/InlinerExtension.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Func/Transforms/FuncConversions.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVDialect.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVOps.h>
#include <mlir/Dialect/SPIRV/IR/SPIRVTypes.h>
#include <mlir/Dialect/SPIRV/IR/TargetAndABI.h>
#include <mlir/Dialect/SPIRV/Transforms/Passes.h>
#include <mlir/Dialect/UB/IR/UBOps.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/IR/SymbolTable.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Transforms/Inliner.h>
#include <mlir/Transforms/InliningUtils.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/STLFunctionalExtras.h>
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
namespace ub = mlir::ub;
namespace vector = mlir::vector;
using llvm::dyn_cast;
using llvm::isa;

namespace {
// Get the type of the raw value of a member of a struct.
mlir::Type getMemberType(mlir::Value Member) {
  if (auto Lift = Member.getDefiningOp<nbdl_spec::LiftStoreOp>())
    return Lift.getValue().getType();
  return Member.getType();
}

struct TypeConverter : mlir::TypeConverter {
  TypeConverter(mlir::ModuleOp M)
    : mlir::TypeConverter()
  {
    addConversion([](mlir::Type T) { return T; });
    // Convert a store with a single alternative to the contained type.
    // Returning a null type indicates failure.
    addConversion([this](nbdl_spec::StoreType ST) -> mlir::Type {
      if (ST.getAlts().size() != 1)
        return mlir::Type();
      mlir::Type AltT = ST.getAlts().front().getValue();
      if (isa<nbdl_spec::CppType>(AltT))
        return mlir::Type();
      return convertType(AltT);
    });
    // Convert a struct to the tuple of its member types.
    addConversion([this, M](nbdl_spec::StructType T) -> mlir::Type {
      auto Def = mlir::ModuleOp(M).lookupSymbol<nbdl_spec::DefineStructOp>(
          T.getName());
      if (!Def)
        return mlir::Type();
      llvm::SmallVector<mlir::Type, 4> Ts;
      for (auto [Name, Member] : Def.getMembers()) {
        mlir::Type MemberT = convertType(getMemberType(Member));
        if (!MemberT)
          return mlir::Type();
        Ts.push_back(MemberT);
      }
      return nbdl_spec::TupleType::get(T.getContext(), Ts);
    });
  }
};

// Leave function signatures to convertFunctionSignatures.
struct ConversionTarget : mlir::ConversionTarget {
  ConversionTarget(mlir::MLIRContext& Ctx, ::TypeConverter const& TC)
    : mlir::ConversionTarget(Ctx)
  {
    addLegalDialect<arith::ArithDialect>();
    addLegalDialect<func::FuncDialect>();
    addLegalDialect<memref::MemRefDialect>();
    addLegalDialect<scf::SCFDialect>();
    addLegalDialect<ub::UBDialect>();
    addIllegalDialect<nbdl_spec::NbdlDialect>();
    addDynamicallyLegalOp<func::CallOp, nbdl_spec::TuplePackOp,
                          nbdl_spec::TupleExtractOp>(
        [&TC](mlir::Operation* Op) { return TC.isLegal(Op); });
    // Erase these after the conversion.
    addLegalOp<nbdl_spec::UnitOp, nbdl_spec::FuncNameOp,
               nbdl_spec::MemberNameOp>();
    addLegalOp<nbdl_spec::DefineStructOp>();
    markOpRecursivelyLegal<nbdl_spec::DefineStructOp>();
  }
};

// Determine if a type that is not a struct can be the type of a member
// of a struct that is lowered.
bool isLowerableMemberType(mlir::Type T) {
  return isa<mlir::IntegerType, mlir::FloatType, mlir::IndexType,
             mlir::VectorType, mlir::MemRefType>(T);
}

// Check that the types used by the functions of a module can be lowered
// (ie they require no C++ and every member of a struct can be lowered.)
class LowerableTypeChecker {
  mlir::ModuleOp M;
  // Map each struct that is checked to whether it can be lowered.
  llvm::DenseMap<mlir::Type, bool> Structs;
  bool HasError = false;

  // Check a struct reporting an error at the location of the user.
  bool checkStruct(mlir::Location Loc, nbdl_spec::StructType T) {
    auto [Itr, Inserted] = Structs.try_emplace(T, false);
    if (!Inserted)
      return Itr->second;
    llvm::StringRef Name = T.getName().getValue();
    auto Def = M.lookupSymbol<nbdl_spec::DefineStructOp>(T.getName());
    if (!Def) {
      mlir::emitError(Loc, "struct is not defined: ") << Name;
      return false;
    }
    for (auto [MemberName, Member] : Def.getMembers()) {
      mlir::Type MemberT = getMemberType(Member);
      if (auto StructT = dyn_cast<nbdl_spec::StructType>(MemberT)) {
        if (!checkStruct(Loc, StructT))
          return false;
      } else if (!isLowerableMemberType(MemberT)) {
        mlir::emitError(Loc, "struct ") << Name << " cannot be lowered since"
          << " member " << MemberName << " has type " << MemberT;
        return false;
      }
    }
    Structs[T] = true;
    return true;
  }

  void check(mlir::Location Loc, mlir::Type T) {
    if (auto ST = dyn_cast<nbdl_spec::StoreType>(T)) {
      for (mlir::TypeAttr Alt : ST.getAlts())
        check(Loc, Alt.getValue());
    } else if (auto StructT = dyn_cast<nbdl_spec::StructType>(T)) {
      if (!checkStruct(Loc, StructT))
        HasError = true;
    } else if (isa<nbdl_spec::CppType, nbdl_spec::CppAliasType>(T)) {
      mlir::emitError(Loc, "type requires C++ and cannot be lowered: ") << T;
      HasError = true;
    }
  }

public:
  LowerableTypeChecker(mlir::ModuleOp M)
    : M(M)
  { }

  llvm::LogicalResult run() {
    for (func::FuncOp F : M.getOps<func::FuncOp>()) {
      for (mlir::Type T : F.getFunctionType().getInputs())
        check(F.getLoc(), T);
      for (mlir::Type T : F.getFunctionType().getResults())
        check(F.getLoc(), T);
      F.getBody().walk([&](mlir::Operation* Op) {
        for (mlir::Type T : Op->getResultTypes())
          check(Op->getLoc(), T);
        for (mlir::Region& Region : Op->getRegions())
          for (mlir::BlockArgument Arg : Region.getArguments())
            check(Op->getLoc(), Arg.getType());
      });
    }
    return llvm::failure(HasError);
  }
};

// Convert the signature of each function (which has raw types.)
llvm::LogicalResult convertFunctionSignatures(mlir::ModuleOp M,
                                              ::TypeConverter const& TC) {
  for (func::FuncOp F : M.getOps<func::FuncOp>()) {
    mlir::FunctionType FT = F.getFunctionType();
    llvm::SmallVector<mlir::Type, 4> InputTs, ResultTs;
    if (llvm::failed(TC.convertTypes(FT.getInputs(), InputTs)) ||
        llvm::failed(TC.convertTypes(FT.getResults(), ResultTs)))
      return F.emitError("function signature cannot be lowered");
    F.setFunctionType(mlir::FunctionType::get(M.getContext(), InputTs,
                                              ResultTs));
    if (F.isExternal())
      continue;
    for (auto [Arg, T] : llvm::zip_equal(F.getArguments(), InputTs))
      Arg.setType(T);
  }
  return llvm::success();
}

// Replace a construct of a struct with the tuple_pack of its members
// as constructed from the arguments by the body of its definition.
struct ExpandStructConstruct
    : mlir::OpRewritePattern<nbdl_spec::ConstructOp> {
  bool& HasError;

  ExpandStructConstruct(mlir::MLIRContext* Ctx, bool& HasError)
    : OpRewritePattern(Ctx),
      HasError(HasError)
  { }

  llvm::LogicalResult matchAndRewrite(nbdl_spec::ConstructOp Op,
                                      mlir::PatternRewriter& R) const final {
    auto StructT = dyn_cast<nbdl_spec::StructType>(Op.getType());
    if (!StructT)
      return llvm::failure();
    auto M = Op->getParentOfType<mlir::ModuleOp>();
    auto Def = M.lookupSymbol<nbdl_spec::DefineStructOp>(StructT.getName());
    if (!Def || Def.isExternal())
      return emitError(Op, "struct is not defined: ") << StructT.getName();
    mlir::Block& Body = Def.getBody().front();
    if (Body.getNumArguments() != Op.getArgs().size())
      return emitError(Op, "expecting ") << Body.getNumArguments()
        << " arguments to construct struct " << StructT.getName();

    // Get the raw value of each member.
    llvm::SmallVector<mlir::Value, 4> Members;
    for (auto [Name, Member] : Def.getMembers()) {
      auto Lift = Member.getDefiningOp<nbdl_spec::LiftStoreOp>();
      if (!Lift)
        return emitError(Op, "unsupported member of struct ")
          << StructT.getName() << ": " << Name;
      Members.push_back(Lift.getValue());
    }

    // Clone only the operations that the members depend on.
    llvm::SmallPtrSet<mlir::Operation*, 8> Needed;
    llvm::SmallVector<mlir::Value, 8> Values(Members.begin(), Members.end());
    while (!Values.empty()) {
      mlir::Operation* ValueOp = Values.pop_back_val().getDefiningOp();
      if (ValueOp && Needed.insert(ValueOp).second)
        llvm::append_range(Values, ValueOp->getOperands());
    }
    mlir::IRMapping Map;
    Map.map(Body.getArguments(), Op.getArgs());
    for (mlir::Operation& BodyOp : Body.without_terminator())
      if (Needed.contains(&BodyOp))
        R.clone(BodyOp, Map);
    llvm::SmallVector<mlir::Value, 4> Elements;
    for (mlir::Value Member : Members)
      Elements.push_back(Map.lookupOrDefault(Member));
    R.replaceOpWithNewOp<nbdl_spec::TuplePackOp>(Op, StructT, Elements);
    return llvm::success();
  }

private:
  // Emit an error that fails the expansion.
  mlir::InFlightDiagnostic emitError(nbdl_spec::ConstructOp Op,
                                     llvm::Twine const& Message) const {
    HasError = true;
    return Op.emitError(Message);
  }
};

// Expand each construct of a struct in the functions of a module.
llvm::LogicalResult expandStructConstructs(mlir::ModuleOp M) {
  bool HasError = false;
  mlir::RewritePatternSet PS(M.getContext());
  PS.add<ExpandStructConstruct>(M.getContext(), HasError);
  mlir::FrozenRewritePatternSet Patterns(std::move(PS));
  mlir::GreedyRewriteConfig Config;
  Config.enableFolding(false);
  Config.setRegionSimplificationLevel(
      mlir::GreedySimplifyRegionLevel::Disabled);
  for (func::FuncOp F : M.getOps<func::FuncOp>())
    if (llvm::failed(mlir::applyPatternsGreedily(F.getBody(), Patterns,
                                                 Config)))
      return F.emitError("struct constructs did not converge");
  return llvm::failure(HasError);
}

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
// Let the outermost loop of a function with results also yield the
// results so a return within it may exit every loop (with poison results
// when continuing a loop.)
//  scf.while (%arg = %init) : (T) -> (T, R) {
//    ...
//    scf.condition(%false) %arg, %result : T, R
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

// Get the types of the values that end a path through the region of Op
// (ie the results of the parent scf.if or scf.execute_region, the values
// that end an iteration of the parent loop, or the results of the parent
// function.)
llvm::SmallVector<mlir::Type, 4> getExitTypes(mlir::Operation* Op) {
  mlir::Operation* Parent = Op->getParentOp();
  if (isa<scf::IfOp, scf::ExecuteRegionOp>(Parent))
    return llvm::to_vector_of<mlir::Type, 4>(Parent->getResultTypes());
  if (auto While = dyn_cast<scf::WhileOp>(Parent))
    return getIterationTypes(While);
  if (auto F = dyn_cast<func::FuncOp>(Parent))
    return llvm::to_vector_of<mlir::Type, 4>(F.getResultTypes());
  return {};
}

// Determine if a scope or match_each has a return that exits it.
bool containsReturn(mlir::Operation* Op) {
  return Op->walk([](nbdl_spec::ReturnOp) {
    return mlir::WalkResult::interrupt();
  }).wasInterrupted();
}

// Get the types of the values that end a path through a scope that
// contains a return (ie the flag to continue after the scope and the
// results of the function.)
llvm::SmallVector<mlir::Type, 4> getScopeExitTypes(mlir::Operation* Op) {
  llvm::SmallVector<mlir::Type, 4> Ts{mlir::IntegerType::get(
                                        Op->getContext(), 1)};
  llvm::append_range(Ts, Op->getParentOfType<func::FuncOp>()
                           .getResultTypes());
  return Ts;
}

// Get the scf.execute_region of the scope that a path through the region
// of Op ends or a null op if it ends the function. Walk up through the
// scf.if of each lowered match_if and the scf.while of each lowered loop.
scf::ExecuteRegionOp getParentScope(mlir::Operation* Op) {
  mlir::Operation* Parent = Op->getParentOp();
  while (isa<scf::IfOp, scf::WhileOp>(Parent))
    Parent = Parent->getParentOp();
  return dyn_cast<scf::ExecuteRegionOp>(Parent);
}

// Append the values of types Ts that end a path through the region of Op
// in its parent scope or function. Pass Results (or poison if there are
// none) preceded by the flag to continue after a scope with results.
void appendEndValues(mlir::ConversionPatternRewriter& R, mlir::Location Loc,
                     mlir::Operation* Op,
                     std::optional<mlir::ValueRange> Results,
                     mlir::TypeRange Ts,
                     llvm::SmallVectorImpl<mlir::Value>& Values) {
  scf::ExecuteRegionOp Scope = getParentScope(Op);
  if (Scope && Scope.getNumResults() != 0) {
    Values.push_back(arith::ConstantIntOp::create(R, Loc, !Results, 1));
    Ts = Ts.drop_front();
  }
  if (Results) {
    llvm::append_range(Values, *Results);
    return;
  }
  for (mlir::Type T : Ts)
    Values.push_back(ub::PoisonOp::create(R, Loc, T));
}

// Append the values that end an iteration of a loop and continue the
// loop Target (if any) with Args or exit every other loop.
// Pass the current values to a loop that does not continue
// since they are ignored. Pass the end values with Results
// (see appendEndValues) to the parent of the outermost loop.
void appendIterationValues(mlir::ConversionPatternRewriter& R,
                           mlir::Location Loc, scf::WhileOp While,
                           scf::WhileOp Target, mlir::ValueRange Args,
                           std::optional<mlir::ValueRange> Results,
                           llvm::SmallVectorImpl<mlir::Value>& Values) {
  bool IsTarget = While == Target;
  Values.push_back(arith::ConstantIntOp::create(R, Loc, IsTarget, 1));
  mlir::ValueRange LoopArgs = IsTarget ? Args : While.getBeforeArguments();
  llvm::append_range(Values, LoopArgs);
  if (scf::WhileOp Parent = getParentLoop(While)) {
    appendIterationValues(R, Loc, Parent, IsTarget ? scf::WhileOp() : Target,
                          Args, Results, Values);
    return;
  }
  appendEndValues(R, Loc, While, Results,
                  mlir::TypeRange(While.getResultTypes())
                    .drop_front(LoopArgs.size()),
                  Values);
}

// End a path through the region of Op with Values (see getExitTypes.)
void createExitTerminator(mlir::ConversionPatternRewriter& R,
                          mlir::Operation* Op, mlir::ValueRange Values) {
  mlir::Location Loc = Op->getLoc();
  mlir::Operation* Parent = Op->getParentOp();
  if (isa<scf::WhileOp>(Parent))
    scf::ConditionOp::create(R, Loc, Values.front(), Values.drop_front());
  else if (isa<func::FuncOp>(Parent))
    func::ReturnOp::create(R, Loc, Values);
  else
    scf::YieldOp::create(R, Loc, Values);
}

// Create the terminator that replaces a terminating nbdl operation
// that neither returns nor continues a loop.
void createTerminator(mlir::ConversionPatternRewriter& R,
                      mlir::Operation* Op) {
  mlir::Location Loc = Op->getLoc();
  llvm::SmallVector<mlir::Value, 4> Values;
  if (scf::WhileOp While = getParentLoop(Op))
    // Exit every loop.
    appendIterationValues(R, Loc, While, scf::WhileOp(), {}, std::nullopt,
                          Values);
  else
    appendEndValues(R, Loc, Op, std::nullopt, getExitTypes(Op), Values);
  createExitTerminator(R, Op, Values);
}

// Terminate the current block with a match_if on the flag of Exit (the
// values that end a scope) that returns the results of Exit in its else
// region. Return the block of its then region with the insertion point
// at its end.
mlir::Block* createExitGuard(mlir::ConversionPatternRewriter& R,
                             mlir::Location Loc, mlir::ValueRange Exit) {
  mlir::Value Continue = Exit.front();
  auto If = nbdl_spec::MatchIfOp::create(R, Loc, Continue);
  mlir::Block* Then = R.createBlock(&If.getThenRegion(), {},
                                    Continue.getType(), Loc);
  R.createBlock(&If.getElseRegion());
  nbdl_spec::ReturnOp::create(R, Loc, Exit.drop_front());
  R.setInsertionPointToEnd(Then);
  return Then;
}

// Return true if a path through the region of Op ends the function
// (ie Op is not within a match_each or scope.)
bool endsFunction(mlir::Operation* Op) {
  mlir::Operation* Parent = Op->getParentOp();
  while (isa<nbdl_spec::MatchIfOp, nbdl_spec::LoopOp>(Parent))
    Parent = Parent->getParentOp();
  return isa<func::FuncOp>(Parent);
}

// Check that every path through a function with results ends with
// a return.
llvm::LogicalResult checkReturns(mlir::ModuleOp M) {
  bool HasError = false;
  M.walk([&](mlir::Operation* Op) {
    if (isa<nbdl_spec::DiscardOp, nbdl_spec::NoOp,
            nbdl_spec::MatchEachOp>(Op) && endsFunction(Op) &&
        Op->getParentOfType<func::FuncOp>().getNumResults() != 0) {
      Op->emitError("expecting a return on every path of a function "
                    "with results");
      HasError = true;
    }
  });
  return llvm::failure(HasError);
}

// Conversion Patterns

struct LowerReturn : mlir::OpConversionPattern<nbdl_spec::ReturnOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ReturnOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    // Exit every loop and scope with the results.
    llvm::SmallVector<mlir::Value, 4> Values;
    if (scf::WhileOp While = getParentLoop(Op))
      appendIterationValues(R, Op.getLoc(), While, scf::WhileOp(), {},
                            Adaptor.getOperands(), Values);
    else
      appendEndValues(R, Op.getLoc(), Op, Adaptor.getOperands(),
                      getExitTypes(Op), Values);
    if (mlir::TypeRange(Values) != mlir::TypeRange(getExitTypes(Op)))
      return R.notifyMatchFailure(Op, "result type mismatch");
    createExitTerminator(R, Op, Values);
    R.eraseOp(Op);
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

// Lower a construct of a type that is not a struct to its argument
// or to zero if it has no arguments.
struct LowerConstruct : mlir::OpConversionPattern<nbdl_spec::ConstructOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ConstructOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type T = getTypeConverter()->convertType(Op.getType());
    mlir::ValueRange Args = Adaptor.getArgs();
    if (T && Args.empty() &&
        isa<mlir::IntegerType, mlir::FloatType, mlir::IndexType,
            mlir::VectorType>(T)) {
      R.replaceOpWithNewOp<arith::ConstantOp>(
          Op, cast<mlir::TypedAttr>(R.getZeroAttr(T)));
      return llvm::success();
    }
    if (T && Args.size() == 1 && Args.front().getType() == T) {
      R.replaceOp(Op, Args.front());
      return llvm::success();
    }
    return Op.emitError("unable to lower construct of ") << Op.getType()
      << " from " << Args.getTypes();
  }
};

// Lower a get of a member of a struct to a tuple_extract.
struct LowerGet : mlir::OpConversionPattern<nbdl_spec::GetOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::GetOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    auto ST = dyn_cast<nbdl_spec::StoreType>(Op.getState().getType());
    auto StructT = ST && ST.getAlts().size() == 1
      ? dyn_cast<nbdl_spec::StructType>(ST.getAlts().front().getValue())
      : nbdl_spec::StructType();
    if (!StructT)
      return R.notifyMatchFailure(Op, "state is not a struct");
    mlir::Type ResultT =
      getTypeConverter()->convertType(Op.getResult().getType());
    mlir::Value State = Adaptor.getState();
    if (Op.hasUnitKey()) {
      if (State.getType() != ResultT)
        return R.notifyMatchFailure(Op, "struct type mismatch");
      R.replaceOp(Op, State);
      return llvm::success();
    }

    auto MemberName = nbdl_spec::lookThroughLoopArgs(Op.getKey())
                        .getDefiningOp<nbdl_spec::MemberNameOp>();
    nbdl_spec::DefineStructOp Def = nbdl_spec::lookupStruct(Op, StructT);
    mlir::Value Member = MemberName && Def
      ? Def.lookupMember(MemberName.getName()) : mlir::Value();
    if (!Member)
      return R.notifyMatchFailure(Op, "struct member not found");
    auto Members = Def.getMembers();
    auto Itr = llvm::find_if(Members, [&](auto const& M) {
      return M.second == Member;
    });
    auto Extract = nbdl_spec::TupleExtractOp::create(
        R, Op.getLoc(), ResultT, State,
        R.getI64IntegerAttr(std::distance(Members.begin(), Itr)));
    R.replaceOp(Op, Extract.getResult());
    return llvm::success();
  }
};

// Convert the types of a tuple_pack.
struct LowerTuplePack : mlir::OpConversionPattern<nbdl_spec::TuplePackOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::TuplePackOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type ResultT = getTypeConverter()->convertType(Op.getType());
    if (!ResultT)
      return R.notifyMatchFailure(Op, "result type conversion failed");
    R.replaceOpWithNewOp<nbdl_spec::TuplePackOp>(Op, ResultT,
                                                 Adaptor.getElements());
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

    // Yield the values that end the path from each branch.
    llvm::SmallVector<mlir::Type, 4> ResultTs = getExitTypes(Op);
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
    createExitTerminator(R, Op, If.getResults());
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Lower the scope to a region with the same terminator semantics.
// Let a scope that contains a return yield the flag to continue and the
// results so that the operations after it run only if it continues.
//  %exit:2 = scf.execute_region -> (i1, R) {
//    ...
//    scf.yield %false, %result : i1, R  // return
//  }
//  nbdl.match_if %exit#0 { <operations after the scope> }
//                else { nbdl.return %exit#1 }
struct LowerScope : mlir::OpConversionPattern<nbdl_spec::ScopeOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::ScopeOp Op, OpAdaptor,
        mlir::ConversionPatternRewriter& R) const override {
    llvm::SmallVector<mlir::Type, 4> ResultTs;
    if (containsReturn(Op))
      ResultTs = getScopeExitTypes(Op);
    auto ExecOp = scf::ExecuteRegionOp::create(R, Op.getLoc(), ResultTs);
    R.inlineRegionBefore(Op.getBody(), ExecOp.getRegion(),
                         ExecOp.getRegion().end());
    if (!ResultTs.empty()) {
      mlir::Block* Rest = R.splitBlock(ExecOp->getBlock(),
                                       std::next(Op->getIterator()));
      R.setInsertionPointAfter(Op);
      mlir::Block* Then = createExitGuard(R, Op.getLoc(),
                                          ExecOp.getResults());
      R.mergeBlocks(Rest, Then);
    }
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
    if (containsReturn(Op))
      return lowerWithExit(Op, R, Range, *BodyBlock, Zero, One);
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

  // Lower a match_each with a return to nested loops that stop when the
  // body exits like a scope (see LowerScope) and return the results.
  //  %exit:3 = scf.while (%i = %c0, %continue = %true, %r = %poison)
  //      : (index, i1, R) -> (index, i1, R) {
  //    %in_bounds = arith.cmpi ult, %i, %dim : index
  //    %cond = arith.andi %in_bounds, %continue : i1
  //    scf.condition(%cond) %i, %continue, %r : index, i1, R
  //  } do {
  //  ^bb0(%i: index, %continue: i1, %r: R):
  //    %element = memref.load %range[%i]
  //    %body:2 = scf.execute_region -> (i1, R) { <body> }
  //    %next = arith.addi %i, %c1 : index
  //    scf.yield %next, %body#0, %body#1 : index, i1, R
  //  }
  //  nbdl.match_if %exit#1 { <end the path> }
  //                else { nbdl.return %exit#2 }
  static llvm::LogicalResult lowerWithExit(
        nbdl_spec::MatchEachOp Op, mlir::ConversionPatternRewriter& R,
        mlir::Value Range, mlir::Block* BodyBlock, mlir::Value Zero,
        mlir::Value One) {
    mlir::Location Loc = Op.getLoc();
    llvm::SmallVector<mlir::Type, 4> ExitTs = getScopeExitTypes(Op);
    llvm::SmallVector<mlir::Type, 4> Ts{R.getIndexType()};
    llvm::append_range(Ts, ExitTs);
    llvm::SmallVector<mlir::Location, 4> Locs(Ts.size(), Loc);

    // Start each loop at zero with the flag to continue.
    llvm::SmallVector<mlir::Value, 4> Inits{
      Zero, arith::ConstantIntOp::create(R, Loc, 1, 1)};
    for (mlir::Type T : llvm::drop_begin(ExitTs))
      Inits.push_back(ub::PoisonOp::create(R, Loc, T));
    auto MRT = cast<mlir::MemRefType>(Range.getType());
    llvm::SmallVector<scf::WhileOp, 4> Whiles;
    llvm::SmallVector<mlir::Value, 4> Indices;
    for (int64_t I = 0; I < MRT.getRank(); ++I) {
      mlir::Value Dim = memref::DimOp::create(R, Loc, Range, I);
      auto While = scf::WhileOp::create(R, Loc, Ts, Inits);
      mlir::Block* Before = R.createBlock(&While.getBefore(), {}, Ts, Locs);
      mlir::Value InBounds = arith::CmpIOp::create(
          R, Loc, arith::CmpIPredicate::ult, Before->getArgument(0), Dim);
      mlir::Value Cond = arith::AndIOp::create(R, Loc, InBounds,
                                               Before->getArgument(1));
      scf::ConditionOp::create(R, Loc, Cond, Before->getArguments());
      mlir::Block* After = R.createBlock(&While.getAfter(), {}, Ts, Locs);
      Whiles.push_back(While);
      Indices.push_back(After->getArgument(0));
      Inits.assign({Zero});
      llvm::append_range(Inits, After->getArguments().drop_front());
    }

    mlir::Value Element = memref::LoadOp::create(R, Loc, Range, Indices);
    auto ExecOp = scf::ExecuteRegionOp::create(R, Loc, ExitTs);
    mlir::Block* ExecBlock = R.createBlock(&ExecOp.getRegion());
    R.mergeBlocks(BodyBlock, ExecBlock, Element);

    // Continue each loop at the next index from the innermost.
    mlir::ValueRange Exit = ExecOp.getResults();
    for (scf::WhileOp While : llvm::reverse(Whiles)) {
      mlir::Block* After = &While.getAfter().front();
      R.setInsertionPointToEnd(After);
      llvm::SmallVector<mlir::Value, 4> Values{
        arith::AddIOp::create(R, Loc, After->getArgument(0), One)};
      llvm::append_range(Values, Exit);
      scf::YieldOp::create(R, Loc, Values);
      Exit = While.getResults().drop_front();
    }

    // nbdl.match_each is a terminator, but scf.while is not.
    R.setInsertionPoint(Op);
    createExitGuard(R, Loc, Exit);
    auto Unit = nbdl_spec::UnitOp::create(
        R, Loc, nbdl_spec::UnitType::get(Op.getContext()));
    nbdl_spec::DiscardOp::create(R, Loc, Unit);
    R.eraseOp(Op);
    return llvm::success();
  }
};

// Lower a loop to a scf.while with the loop-carried values as its
// iteration arguments. End the path through the parent region with the
// results that follow the loop-carried values. (See getParentLoop.)
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
    llvm::SmallVector<mlir::Type, 4> ResultTs(Ts);
    llvm::append_range(ResultTs, getExitTypes(Op));
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
    createExitTerminator(R, Op, While.getResults().drop_front(Ts.size()));
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
    appendIterationValues(R, Op.getLoc(), While, Target, Args, {}, Values);
    createExitTerminator(R, Op, Values);
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
         LowerConstruct,
         LowerGet,
         LowerTuplePack,
         LowerVisit,
         LowerMatchIf,
         LowerMatchEach,
         LowerScope,
         LowerLoop,
         LowerContinue
         >(TC, Ctx);
  // Convert the calls with struct types that the flatten pass creates.
  mlir::populateCallOpTypeConversionPattern(PS, TC);
}

// Lowering Passes

class LowerPass : public nbdl_spec::impl::LowerPassBase<LowerPass> {
  using Base = nbdl_spec::impl::LowerPassBase<LowerPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::MLIRContext* Ctx = &getContext();
    mlir::ModuleOp M = getOperation();
    if (llvm::failed(checkReturns(M)) ||
        llvm::failed(LowerableTypeChecker(M).run()))
      return signalPassFailure();

    ::TypeConverter TC(M);
    if (llvm::failed(convertFunctionSignatures(M, TC)) ||
        llvm::failed(expandStructConstructs(M)))
      return signalPassFailure();
    ::ConversionTarget Target(*Ctx, TC);
    mlir::RewritePatternSet PS(Ctx);
    populateLowerPatterns(PS, TC);

    mlir::ConversionConfig Config;
    Config.allowPatternRollback = false;
    if (llvm::failed(mlir::applyPartialConversion(M, Target, std::move(PS),
                                                  Config)))
      return signalPassFailure();

    // Erase the remaining operations that were only used by nbdl operations.
    M.walk([](mlir::Operation* Op) {
      if (isa<nbdl_spec::UnitOp, nbdl_spec::FuncNameOp,
              nbdl_spec::MemberNameOp>(Op) && Op->use_empty())
        Op->erase();
      else if (isa<scf::WhileOp>(Op))
        Op->removeAttr(LoopFnAttrName);
    });
    // Erase the struct definitions since their types are converted.
    for (auto Def : llvm::make_early_inc_range(
           M.getOps<nbdl_spec::DefineStructOp>()))
      Def.erase();
  }
};

// Lower a tuple_pack to the insertion of each element into a poison struct.
struct TuplePackToLLVM
    : mlir::ConvertOpToLLVMPattern<nbdl_spec::TuplePackOp> {
  using ConvertOpToLLVMPattern::ConvertOpToLLVMPattern;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::TuplePackOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type T = getTypeConverter()->convertType(Op.getType());
    if (!T)
      return R.notifyMatchFailure(Op, "tuple type conversion failed");
    mlir::Location Loc = Op.getLoc();
    mlir::Value Result = mlir::LLVM::PoisonOp::create(R, Loc, T);
    for (auto [I, Element] : llvm::enumerate(Adaptor.getElements()))
      Result = mlir::LLVM::InsertValueOp::create(
          R, Loc, Result, Element, llvm::ArrayRef<int64_t>{int64_t(I)});
    R.replaceOp(Op, Result);
    return llvm::success();
  }
};

struct TupleExtractToLLVM
    : mlir::ConvertOpToLLVMPattern<nbdl_spec::TupleExtractOp> {
  using ConvertOpToLLVMPattern::ConvertOpToLLVMPattern;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::TupleExtractOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
        Op, Adaptor.getTuple(),
        llvm::ArrayRef<int64_t>{int64_t(Op.getIndex())});
    return llvm::success();
  }
};

// Convert a tuple to a literal LLVM struct of its element types.
void populateTupleToLLVM(mlir::LLVMTypeConverter& TC,
                         mlir::RewritePatternSet& PS) {
  TC.addConversion(
      [&TC](nbdl_spec::TupleType T) -> std::optional<mlir::Type> {
        llvm::SmallVector<mlir::Type, 4> Ts;
        if (llvm::failed(TC.convertTypes(T.getTypes(), Ts)))
          return mlir::Type();
        return mlir::LLVM::LLVMStructType::getLiteral(T.getContext(), Ts);
      });
  PS.add<TuplePackToLLVM, TupleExtractToLLVM>(TC);
}

// Lower the mid level dialects that nbdl-lower produces to LLVM.
class LowerToLLVMPass
    : public nbdl_spec::impl::LowerToLLVMPassBase<LowerToLLVMPass> {
  using Base = nbdl_spec::impl::LowerToLLVMPassBase<LowerToLLVMPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    if (llvm::failed(schir_common::lowerToLLVM(getOperation(),
                                               populateTupleToLLVM)))
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
  spirv::PointerType PtrT;
  if (*SC == spirv::StorageClass::StorageBuffer) {
    // Pass a storage buffer as the pointer of its memref.
    auto MemRefT = dyn_cast<mlir::MemRefType>(T);
    if (MemRefT && MemRefT.getLayout().isIdentity())
      PtrT = dyn_cast_or_null<spirv::PointerType>(TC.convertType(T));
    if (!PtrT || PtrT.getStorageClass() != *SC) {
      F.emitError("unsupported shader storage buffer type: ") << T;
      return {};
    }
  } else {
    // Pass only scalars and vectors that are the same type in SPIRV.
    if (!isa<mlir::IntegerType, mlir::FloatType, mlir::VectorType>(T) ||
        TC.convertType(T) != T) {
      F.emitError("unsupported shader interface type: ") << T;
      return {};
    }
    // Wrap a uniform buffer in a struct as Vulkan requires.
    mlir::Type PointeeT = T;
    if (*SC == spirv::StorageClass::Uniform)
      PointeeT = spirv::StructType::get({T}, {0});
    PtrT = spirv::PointerType::get(PointeeT, *SC);
  }

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
  if (auto A = Desc.getAs<mlir::UnitAttr>("non_writable"))
    Var->setAttr("non_writable", A);
  return Var;
}

// Load the value of a parameter of type T of a shader entry from its
// variable or cast the pointer of a storage buffer to its memref.
mlir::Value loadInterfaceVar(mlir::OpBuilder& B, mlir::Location Loc,
                             mlir::Type T, spirv::GlobalVariableOp Var) {
  mlir::Value Ptr = spirv::AddressOfOp::create(B, Loc, Var);
  auto PtrT = cast<spirv::PointerType>(Var.getType());
  if (PtrT.getStorageClass() == spirv::StorageClass::StorageBuffer)
    return mlir::UnrealizedConversionCastOp::create(B, Loc, T, Ptr)
      .getResult(0);
  if (isa<spirv::StructType>(PtrT.getPointeeType())) {
    mlir::Value Zero = spirv::ConstantOp::getZero(B.getI32Type(), Loc, B);
    Ptr = spirv::AccessChainOp::create(B, Loc, Ptr, Zero);
  }
  return spirv::LoadOp::create(B, Loc, Ptr);
}

// Create the uniform buffer of the lengths of the runtime-sized storage
// buffers of a shader entry and load each dynamic size from it.
// Take each buffer as the memref loaded from a variable and its description.
llvm::LogicalResult lowerBufferLengths(
    mlir::OpBuilder& B, func::FuncOp F, llvm::StringRef Name,
    mlir::DictionaryAttr Desc,
    llvm::ArrayRef<std::pair<mlir::Value, mlir::DictionaryAttr>> Buffers) {
  // Map each runtime-sized storage buffer to its member of the struct.
  llvm::SmallVector<std::pair<mlir::Value, int64_t>, 4> Lengths;
  for (auto [Buffer, BufferDesc] : Buffers) {
    auto Member = BufferDesc.getAs<mlir::IntegerAttr>("length");
    if (!Member)
      continue;
    auto MemRefT = dyn_cast<mlir::MemRefType>(Buffer.getType());
    if (!MemRefT || MemRefT.getNumDynamicDims() != 1)
      return F.emitError("unsupported shader storage buffer type: ")
             << Buffer.getType();
    Lengths.emplace_back(Buffer, Member.getInt());
  }
  auto Set = Desc.getAs<mlir::IntegerAttr>("descriptor_set");
  auto Binding = Desc.getAs<mlir::IntegerAttr>("binding");
  int64_t NumMembers = Lengths.size();
  if (!Set || !Binding || NumMembers == 0 ||
      llvm::any_of(Lengths, [&](auto const& L) {
        return L.second < 0 || L.second >= NumMembers;
      }))
    return F.emitError("invalid shader length buffer: ") << Desc;

  // Lay out the lengths as a struct of i32 since an array of i32 in a
  // uniform buffer requires a stride of 16.
  mlir::Type I32 = B.getI32Type();
  llvm::SmallVector<mlir::Type, 4> MemberTs(NumMembers, I32);
  llvm::SmallVector<spirv::StructType::OffsetInfo, 4> Offsets;
  for (int64_t I = 0; I < NumMembers; ++I)
    Offsets.push_back(I * 4);
  auto PtrT = spirv::PointerType::get(
      spirv::StructType::get(MemberTs, Offsets), spirv::StorageClass::Uniform);
  auto Var = spirv::GlobalVariableOp::create(
      B, F.getLoc(), mlir::TypeAttr::get(PtrT),
      B.getStringAttr(Name + "_lengths"));
  Var.setDescriptorSetAttr(Set);
  Var.setBindingAttr(Binding);

  // Replace each memref.dim of a dynamic size with its loaded length.
  for (auto [Buffer, Member] : Lengths) {
    auto MemRefT = cast<mlir::MemRefType>(Buffer.getType());
    for (mlir::Operation* User :
         llvm::make_early_inc_range(Buffer.getUsers())) {
      auto Dim = dyn_cast<memref::DimOp>(User);
      if (!Dim)
        continue;
      std::optional<int64_t> Index = Dim.getConstantIndex();
      if (!Index || !MemRefT.isDynamicDim(*Index))
        continue;
      mlir::OpBuilder DimB(Dim);
      mlir::Location Loc = Dim.getLoc();
      mlir::Value Ptr = spirv::AddressOfOp::create(DimB, Loc, Var);
      mlir::Value MemberIndex = spirv::ConstantOp::create(
          DimB, Loc, I32, DimB.getI32IntegerAttr(Member));
      Ptr = spirv::AccessChainOp::create(DimB, Loc, Ptr, MemberIndex);
      mlir::Value Length = spirv::LoadOp::create(DimB, Loc, Ptr);
      Dim.replaceAllUsesWith(arith::IndexCastOp::create(
          DimB, Loc, DimB.getIndexType(), Length).getResult());
      Dim.erase();
    }
  }
  return llvm::success();
}

// Determine if a type is a tuple with a memref (possibly nested.)
bool isTupleWithMemRef(mlir::Type T) {
  auto TupleT = dyn_cast<nbdl_spec::TupleType>(T);
  return TupleT && llvm::any_of(TupleT.getTypes(), [](mlir::Type ElT) {
    return isa<mlir::MemRefType>(ElT) || isTupleWithMemRef(ElT);
  });
}

// Replace each tuple_extract of a tuple_pack with the element and erase
// each tuple_pack that has no uses.
void foldTuples(mlir::Operation* Scope) {
  llvm::SmallVector<nbdl_spec::TuplePackOp, 4> Packs;
  Scope->walk([&](mlir::Operation* Op) {
    if (auto Extract = dyn_cast<nbdl_spec::TupleExtractOp>(Op)) {
      if (auto Pack =
            Extract.getTuple().getDefiningOp<nbdl_spec::TuplePackOp>()) {
        Extract.replaceAllUsesWith(Pack.getElements()[Extract.getIndex()]);
        Extract.erase();
      }
    } else if (auto Pack = dyn_cast<nbdl_spec::TuplePackOp>(Op)) {
      Packs.push_back(Pack);
    }
  });
  for (nbdl_spec::TuplePackOp Pack : llvm::reverse(Packs))
    if (Pack->use_empty())
      Pack.erase();
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
  auto CreateVar = [&](mlir::Attribute VarDesc, llvm::Twine const& VarName,
                       mlir::Type T) -> spirv::GlobalVariableOp {
    spirv::GlobalVariableOp Var = createInterfaceVar(
        B, F, VarName, T, dyn_cast<mlir::DictionaryAttr>(VarDesc), TC);
    if (!Var)
      return {};
    spirv::StorageClass SC =
      cast<spirv::PointerType>(Var.getType()).getStorageClass();
    if (SC == spirv::StorageClass::Input ||
        SC == spirv::StorageClass::Output)
      Entry.InterfaceVars.push_back(
        mlir::FlatSymbolRefAttr::get(Var.getSymNameAttr()));
    return Var;
  };
  // Create a variable for each member of a struct of storage buffers
  // (described by an array.)
  using VarList = llvm::SmallVector<spirv::GlobalVariableOp, 1>;
  auto CreateVars = [&](mlir::ArrayAttr Descs, mlir::TypeRange Ts,
                        llvm::StringRef Suffix,
                        llvm::SmallVectorImpl<VarList>& Vars)
                        -> llvm::LogicalResult {
    for (auto [I, VarDesc] : llvm::enumerate(Descs)) {
      std::string VarName = (Name.getValue() + Suffix + llvm::Twine(I)).str();
      VarList& List = Vars.emplace_back();
      auto MemberDescs = dyn_cast<mlir::ArrayAttr>(VarDesc);
      if (!MemberDescs) {
        List.push_back(CreateVar(VarDesc, VarName, Ts[I]));
      } else {
        auto TupleT = dyn_cast<nbdl_spec::TupleType>(Ts[I]);
        if (!TupleT || TupleT.getTypes().size() != MemberDescs.size())
          return F.emitError("invalid shader struct of storage buffers: ")
                 << Ts[I];
        for (auto [J, MemberDesc] : llvm::enumerate(MemberDescs))
          List.push_back(CreateVar(MemberDesc,
                                   VarName + "_" + llvm::Twine(J),
                                   TupleT.getTypes()[J]));
      }
      if (llvm::is_contained(List, spirv::GlobalVariableOp()))
        return llvm::failure();
    }
    return llvm::success();
  };
  llvm::SmallVector<VarList, 4> InputVars, OutputVars;
  if (llvm::failed(CreateVars(InputDescs, FT.getInputs(), "_arg",
                              InputVars)) ||
      llvm::failed(CreateVars(OutputDescs, FT.getResults(), "_result",
                              OutputVars)))
    return llvm::failure();

  // Load the parameters at the beginning of the body and pack the members
  // of each struct.
  mlir::Block& Body = F.front();
  mlir::OpBuilder BodyB = mlir::OpBuilder::atBlockBegin(&Body);
  mlir::Location Loc = F.getLoc();
  llvm::SmallVector<std::pair<mlir::Value, mlir::DictionaryAttr>, 4> Buffers;
  for (auto [Arg, Vars, InputDesc] :
       llvm::zip_equal(Body.getArguments(), InputVars, InputDescs)) {
    auto MemberDescs = dyn_cast<mlir::ArrayAttr>(InputDesc);
    if (!MemberDescs) {
      mlir::Value Value = loadInterfaceVar(BodyB, Loc, Arg.getType(),
                                           Vars.front());
      Buffers.emplace_back(Value, cast<mlir::DictionaryAttr>(InputDesc));
      Arg.replaceAllUsesWith(Value);
      continue;
    }
    auto TupleT = cast<nbdl_spec::TupleType>(Arg.getType());
    llvm::SmallVector<mlir::Value, 4> Members;
    for (auto [T, Var, MemberDesc] :
         llvm::zip_equal(TupleT.getTypes(), Vars, MemberDescs)) {
      Members.push_back(loadInterfaceVar(BodyB, Loc, T, Var));
      Buffers.emplace_back(Members.back(),
                           cast<mlir::DictionaryAttr>(MemberDesc));
    }
    Arg.replaceAllUsesWith(
      nbdl_spec::TuplePackOp::create(BodyB, Loc, TupleT, Members));
  }
  Body.eraseArguments(0, Body.getNumArguments());
  foldTuples(F);

  if (auto LengthDesc = Desc.getAs<mlir::DictionaryAttr>("length_buffer"))
    if (llvm::failed(lowerBufferLengths(B, F, Name.getValue(), LengthDesc,
                                        Buffers)))
      return llvm::failure();

  // Store the results at each return.
  llvm::SmallVector<func::ReturnOp, 4> Returns;
  F.walk([&](func::ReturnOp Ret) { Returns.push_back(Ret); });
  for (func::ReturnOp Ret : Returns) {
    B.setInsertionPoint(Ret);
    for (auto [Result, Vars] :
         llvm::zip_equal(Ret.getOperands(), OutputVars)) {
      mlir::Value Ptr = spirv::AddressOfOp::create(B, Ret.getLoc(),
                                                   Vars.front());
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

// Inline each call within Scope that has an operand whose type satisfies
// Pred until there are none.
template <typename CallOpT, typename FuncOpT>
llvm::LogicalResult inlineCalls(mlir::Operation* Scope,
                                llvm::function_ref<bool(mlir::Type)> Pred) {
  mlir::InlinerInterface Interface(Scope->getContext());
  mlir::InlinerConfig Config;
  llvm::SmallVector<CallOpT, 4> Calls;
  do {
    Calls.clear();
    Scope->walk([&](CallOpT Call) {
      if (llvm::any_of(Call->getOperandTypes(), Pred))
        Calls.push_back(Call);
    });
    for (CallOpT Call : Calls) {
      auto Callee = mlir::SymbolTable::lookupNearestSymbolFrom<FuncOpT>(
          Call, Call.getCalleeAttr());
      if (!Callee || Callee.isExternal() ||
          llvm::failed(mlir::inlineCall(Interface, Config.getCloneCallback(),
                                        Call, Callee, &Callee.getBody())))
        return Call.emitError("cannot inline call with a storage buffer");
      Call.erase();
    }
  } while (!Calls.empty());
  return llvm::success();
}

// Inline each call with a storage buffer pointer which a call cannot pass
// without variable pointers.
llvm::LogicalResult inlineStorageBufferCalls(spirv::ModuleOp SPIRVModule) {
  return inlineCalls<spirv::FunctionCallOp, spirv::FuncOp>(
      SPIRVModule, [](mlir::Type T) {
        auto PtrT = dyn_cast<spirv::PointerType>(T);
        return PtrT &&
               PtrT.getStorageClass() == spirv::StorageClass::StorageBuffer;
      });
}

// Lower a tuple_pack to a spirv.CompositeConstruct.
struct TuplePackToSPIRV : mlir::OpConversionPattern<nbdl_spec::TuplePackOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::TuplePackOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    mlir::Type T = getTypeConverter()->convertType(Op.getType());
    if (!T)
      return R.notifyMatchFailure(Op, "tuple type conversion failed");
    R.replaceOpWithNewOp<spirv::CompositeConstructOp>(Op, T,
                                                      Adaptor.getElements());
    return llvm::success();
  }
};

struct TupleExtractToSPIRV
    : mlir::OpConversionPattern<nbdl_spec::TupleExtractOp> {
  using Base::Base;

  llvm::LogicalResult matchAndRewrite(
        nbdl_spec::TupleExtractOp Op, OpAdaptor Adaptor,
        mlir::ConversionPatternRewriter& R) const override {
    R.replaceOpWithNewOp<spirv::CompositeExtractOp>(
        Op, Adaptor.getTuple(),
        llvm::ArrayRef<int32_t>{int32_t(Op.getIndex())});
    return llvm::success();
  }
};

// Convert a tuple to a spirv.struct (with no layout) of its element types
// unless it has a pointer which a composite cannot have.
void populateTupleToSPIRV(schir_common::SPIRVTypeConverter& TC,
                          mlir::RewritePatternSet& PS) {
  TC.addConversion(
      [&TC](nbdl_spec::TupleType T) -> std::optional<mlir::Type> {
        llvm::SmallVector<mlir::Type, 4> Ts;
        if (llvm::failed(TC.convertTypes(T.getTypes(), Ts)) ||
            llvm::any_of(Ts, llvm::IsaPred<spirv::PointerType>))
          return mlir::Type();
        return spirv::StructType::get(Ts);
      });
  PS.add<TuplePackToSPIRV, TupleExtractToSPIRV>(TC, PS.getContext());
}

// Determine if a function has a tuple with a memref in its signature.
bool hasTupleWithMemRef(func::FuncOp F) {
  mlir::FunctionType FT = F.getFunctionType();
  return llvm::any_of(FT.getInputs(), isTupleWithMemRef) ||
         llvm::any_of(FT.getResults(), isTupleWithMemRef);
}

// Inline each call with a struct of memrefs and erase each function with
// one in its signature since a struct cannot hold a storage buffer.
llvm::LogicalResult inlineTuplesWithMemRefs(mlir::ModuleOp M) {
  if (llvm::failed(inlineCalls<func::CallOp, func::FuncOp>(
          M, [](mlir::Type T) { return isTupleWithMemRef(T); })))
    return llvm::failure();
  for (func::FuncOp F : llvm::make_early_inc_range(M.getOps<func::FuncOp>()))
    if (hasTupleWithMemRef(F) &&
        !F->hasAttr(nbdl_spec::ShaderEntryAttrName))
      F.erase();
  return llvm::success();
}

// Lower the mid level dialects that nbdl-lower produces to SPIRV.
class LowerToSPIRVPass
    : public nbdl_spec::impl::LowerToSPIRVPassBase<LowerToSPIRVPass> {
  using Base = nbdl_spec::impl::LowerToSPIRVPassBase<LowerToSPIRVPass>;

public:
  using Base::Base;

  void getDependentDialects(mlir::DialectRegistry& Registry) const override {
    Base::getDependentDialects(Registry);
    func::registerInlinerExtension(Registry);
    mlir::LLVM::registerInlinerInterface(Registry);
  }

  void runOnOperation() override {
    mlir::ModuleOp M = getOperation();
    spirv::TargetEnvAttr TEA = getVulkanTargetEnv();
    M->setAttr(spirv::getTargetEnvAttrName(), TEA);

    // Map memref memory spaces to the Vulkan storage classes.
    spirv::MemorySpaceToStorageClassConverter MSC(
        spirv::mapMemorySpaceToVulkanStorageClass);
    spirv::convertMemRefTypesAndAttrs(M, MSC);

    // Turn the marked functions into entry point functions with each call
    // with a memref inlined so the sizes of its storage buffers are known.
    if (llvm::failed(inlineTuplesWithMemRefs(M)))
      return signalPassFailure();
    schir_common::SPIRVTypeConverter TC(TEA);
    llvm::SmallVector<ShaderEntry, 4> Entries;
    for (func::FuncOp F :
         llvm::make_early_inc_range(M.getOps<func::FuncOp>())) {
      auto Desc = F->getAttrOfType<mlir::DictionaryAttr>(
          nbdl_spec::ShaderEntryAttrName);
      if (!Desc)
        continue;
      if (llvm::failed(inlineCalls<func::CallOp, func::FuncOp>(
              F, llvm::IsaPred<mlir::MemRefType>)))
        return signalPassFailure();
      llvm::FailureOr<ShaderEntry> Entry = createShaderEntry(F, Desc, TC);
      if (llvm::failed(Entry))
        return signalPassFailure();
      Entries.push_back(std::move(*Entry));
    }

    // Reject the remaining dynamic memref sizes which have no SPIRV lowering.
    mlir::WalkResult DimResult = M.walk([](memref::DimOp Op) {
      std::optional<int64_t> Index = Op.getConstantIndex();
      auto SourceT = cast<mlir::ShapedType>(Op.getSource().getType());
      if (Index && !SourceT.isDynamicDim(*Index))
        return mlir::WalkResult::advance();
      Op.emitError("dynamic memref size is not supported in SPIRV");
      return mlir::WalkResult::interrupt();
    });
    if (DimResult.wasInterrupted())
      return signalPassFailure();

    // Reject the remaining structs of memrefs.
    foldTuples(M);
    mlir::WalkResult TupleResult = M.walk([](mlir::Operation* Op) {
      if (llvm::none_of(Op->getResultTypes(), isTupleWithMemRef))
        return mlir::WalkResult::advance();
      Op->emitError("struct of memrefs is not supported in SPIRV"
                    " except as a parameter of a shader entry");
      return mlir::WalkResult::interrupt();
    });
    if (TupleResult.wasInterrupted())
      return signalPassFailure();

    if (llvm::failed(schir_common::lowerToSPIRV(M, populateTupleToSPIRV)))
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
    if (llvm::failed(inlineStorageBufferCalls(SPIRVModule)))
      return signalPassFailure();

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
