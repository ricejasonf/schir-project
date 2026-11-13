#include <schlap/Passes.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/IR/SymbolTable.h>
#include <mlir/Transforms/RegionUtils.h>
#include <mlir/Transforms/WalkPatternRewriteDriver.h>
#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/ADT/TypeSwitch.h>

namespace schlap {
#define GEN_PASS_DEF_NORMALIZEACTIONSPASS
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

using mlir::Block;
using mlir::IRMapping;
using mlir::Operation;
using mlir::Value;

namespace {
// Rewrite a ≠ b as ¬(a = b).
struct RewriteNe : mlir::OpRewritePattern<schlap::NeOp> {
  using OpRewritePattern::OpRewritePattern;

  llvm::LogicalResult
  matchAndRewrite(schlap::NeOp Op,
                  mlir::PatternRewriter& Rewriter) const override {
    Value Eq = schlap::EqOp::create(Rewriter, Op.getLoc(), Op.getType(),
                                    Op.getLhs(), Op.getRhs());
    Rewriter.replaceOpWithNewOp<schlap::NotOp>(Op, Op.getType(), Eq);
    return mlir::success();
  }
};

// Rewrite a ∉ S as ¬(a ∈ S).
struct RewriteNotMember : mlir::OpRewritePattern<schlap::NotMemberOp> {
  using OpRewritePattern::OpRewritePattern;

  llvm::LogicalResult
  matchAndRewrite(schlap::NotMemberOp Op,
                  mlir::PatternRewriter& Rewriter) const override {
    Value Member = schlap::MemberOp::create(Rewriter, Op.getLoc(),
                                            Op.getType(), Op.getLhs(),
                                            Op.getRhs());
    Rewriter.replaceOpWithNewOp<schlap::NotOp>(Op, Op.getType(), Member);
    return mlir::success();
  }
};

// Rewrite unchanged x y as (′ x) = x ∧ (′ y) = y.
struct RewriteUnchanged : mlir::OpRewritePattern<schlap::UnchangedOp> {
  using OpRewritePattern::OpRewritePattern;

  llvm::LogicalResult
  matchAndRewrite(schlap::UnchangedOp Op,
                  mlir::PatternRewriter& Rewriter) const override {
    mlir::Location Loc = Op.getLoc();
    mlir::Type Type = Op.getType();
    Value Result;
    for (Value Var : Op.getVars()) {
      Value Prime = schlap::PrimeOp::create(Rewriter, Loc, Var.getType(), Var);
      Value Eq = schlap::EqOp::create(Rewriter, Loc, Type, Prime, Var);
      Result = Result ? schlap::AndOp::create(Rewriter, Loc, Type, Result, Eq)
                      : Eq;
    }
    if (!Result)
      Result = schlap::TrueOp::create(Rewriter, Loc, Type);
    Rewriter.replaceOp(Op, Result);
    return mlir::success();
  }
};

// Return true if Op holds a formula of the specification.
bool isFormulaRegionOp(Operation* Op) {
  return isa<schlap::InitOp, schlap::ActionOp, schlap::NextOp,
             schlap::InvariantOp>(Op);
}

// Return the value yielded by a formula region or null.
Value getFormula(Operation* Op) {
  Block& Body = Op->getRegion(0).front();
  if (auto Yield = dyn_cast<schlap::YieldOp>(Body.getTerminator()))
    return Yield.getValue();
  return nullptr;
}

// Clone the definition of V and what it uses at the builder unless Keep
// holds for V or V is mapped already.
Value materialize(Value V, IRMapping& Mapping, mlir::OpBuilder& Builder,
                  llvm::function_ref<bool(Value)> Keep) {
  if (Value Mapped = Mapping.lookupOrNull(V))
    return Mapped;
  Operation* Def = V.getDefiningOp();
  if (Keep(V) || !Def)
    return V;
  for (Value Operand : Def->getOperands())
    materialize(Operand, Mapping, Builder, Keep);
  llvm::SetVector<Value> Above;
  mlir::getUsedValuesDefinedAbove(Def->getRegions(), Above);
  for (Value Operand : Above)
    materialize(Operand, Mapping, Builder, Keep);
  Builder.clone(*Def, Mapping);
  return Mapping.lookup(V);
}

// Return true if V is a state variable.
bool isVariable(Value V) {
  return V.getDefiningOp<schlap::VariableOp>() != nullptr;
}

// Copy the definitions in the specification body that a formula region
// uses into the region and erase them from the body.
void sinkDefinitions(schlap::SpecOp Spec) {
  Block& Body = Spec.getBody().front();
  for (Operation& Op : Body) {
    if (!isFormulaRegionOp(&Op))
      continue;
    mlir::Region& Region = Op.getRegion(0);
    llvm::SetVector<Value> Above;
    mlir::getUsedValuesDefinedAbove(Region, Above);
    mlir::OpBuilder Builder = mlir::OpBuilder::atBlockBegin(&Region.front());
    IRMapping Mapping;
    for (Value V : Above) {
      Value New = materialize(V, Mapping, Builder, isVariable);
      if (New != V)
        mlir::replaceAllUsesInRegionWith(V, New, Region);
    }
  }
  for (Operation& Op : llvm::make_early_inc_range(llvm::reverse(Body)))
    if (!isFormulaRegionOp(&Op) && !isa<schlap::VariableOp>(Op) &&
        Op.use_empty())
      Op.erase();
}

// Report a next-state value outside an action or next.
llvm::LogicalResult checkPrimes(schlap::SpecOp Spec) {
  bool Failed = false;
  Spec.walk([&](Operation* Op) {
    if (!isa<schlap::PrimeOp, schlap::ActionRefOp>(Op))
      return;
    Operation* Region = Op->getParentOp();
    while (!isFormulaRegionOp(Region) && !isa<schlap::SpecOp>(Region))
      Region = Region->getParentOp();
    if (isa<schlap::ActionOp, schlap::NextOp>(Region))
      return;
    if (isa<schlap::PrimeOp>(Op))
      Op->emitError("′ is only valid in an action or next");
    else
      Op->emitError("an action is only valid in an action or next");
    Failed = true;
  });
  return mlir::failure(Failed);
}

// Rewrite the formula of init or next into statements.
class Converter {
public:
  Converter(schlap::SpecOp Spec, Operation* Root)
    : Spec(Spec), Root(Root), IsInit(isa<schlap::InitOp>(Root)),
      SymbolTable(Spec), Builder(Spec.getContext()) {
    for (auto Var : Spec.getBody().getOps<schlap::VariableOp>()) {
      VarIndex[Var.getResult()] = Vars.size();
      Vars.push_back(Var);
    }
  }

  llvm::LogicalResult run() {
    Value Formula = getFormula(Root);
    if (!Formula)
      return mlir::success();

    Block* NewBody = new Block();
    Builder.setInsertionPointToEnd(NewBody);
    Paths P{Path{llvm::BitVector(Vars.size()), nullptr}};
    IRMapping Mapping;
    convert(Formula, P, Mapping);
    schlap::DoneOp::create(Builder, Root->getLoc());
    if (!Failed)
      checkAssigned(P);

    if (Failed) {
      NewBody->dropAllDefinedValueUses();
      delete NewBody;
      return mlir::failure();
    }
    mlir::Region& Region = Root->getRegion(0);
    Block& OldBody = Region.front();
    OldBody.dropAllDefinedValueUses();
    OldBody.erase();
    Region.push_back(NewBody);
    return mlir::success();
  }

private:
  struct Path {
    llvm::BitVector Assigned;
    Operation* Action;

    bool operator==(Path const& Other) const {
      return Action == Other.Action && Assigned == Other.Assigned;
    }
  };
  using Paths = llvm::SmallVector<Path, 4>;

  schlap::SpecOp Spec;
  Operation* Root;
  bool IsInit;
  mlir::SymbolTable SymbolTable;
  mlir::OpBuilder Builder;
  llvm::DenseMap<Value, unsigned> VarIndex;
  llvm::SmallVector<schlap::VariableOp> Vars;
  llvm::DenseMap<Value, llvm::BitVector> ReadsCache;
  llvm::SmallPtrSet<Operation*, 4> ActionStack;
  bool Failed = false;

  // Return the variable whose next value V is.
  std::optional<unsigned> getNextRef(Value V) {
    if (!IsInit) {
      auto Prime = V.getDefiningOp<schlap::PrimeOp>();
      if (!Prime)
        return std::nullopt;
      V = Prime.getVar();
    }
    auto It = VarIndex.find(V);
    if (It == VarIndex.end())
      return std::nullopt;
    return It->second;
  }

  // Return the variables whose next values F reads, and ActionBit if F
  // refers to an action.
  llvm::BitVector getReads(Value F) {
    auto It = ReadsCache.find(F);
    if (It != ReadsCache.end())
      return It->second;
    unsigned ActionBit = Vars.size();
    llvm::BitVector Reads(ActionBit + 1);
    ReadsCache[F] = Reads;
    if (std::optional<unsigned> I = getNextRef(F))
      Reads.set(*I);
    if (Operation* Def = F.getDefiningOp()) {
      for (Value Operand : Def->getOperands())
        Reads |= getReads(Operand);
      Def->walk([&](Operation* Inner) {
        if (isa<schlap::ActionRefOp>(Inner))
          Reads.set(ActionBit);
        if (Inner == Def)
          return;
        for (Value Operand : Inner->getOperands())
          Reads |= getReads(Operand);
      });
    }
    ReadsCache[F] = Reads;
    return Reads;
  }

  schlap::ActionOp lookupAction(schlap::ActionRefOp Ref) {
    return SymbolTable.lookup<schlap::ActionOp>(Ref.getAction());
  }

  bool isAssignedOnAll(unsigned I, Paths const& P) {
    if (I == Vars.size())
      return false;
    return llvm::all_of(P, [&](Path const& X) { return X.Assigned.test(I); });
  }

  // Return true if F reads a next value not set on every path.
  bool readsUnassigned(Value F, Paths const& P) {
    for (unsigned I : getReads(F).set_bits())
      if (!isAssignedOnAll(I, P))
        return true;
    return false;
  }

  void markAssigned(unsigned I, Paths& P) {
    for (Path& X : P)
      X.Assigned.set(I);
  }

  Value materialize(Value V, IRMapping& Mapping) {
    return ::materialize(V, Mapping, Builder, isVariable);
  }

  // Return the name of the value variable I has in the next state.
  std::string getNextName(unsigned I) {
    std::string Name = Vars[I].getName().str();
    return IsInit ? Name : "′ " + Name;
  }

  // Create a guard for F reporting the next values it reads too early.
  void guard(Value F, Paths const& P, IRMapping& Mapping) {
    for (unsigned I : getReads(F).set_bits())
      if (!isAssignedOnAll(I, P)) {
        Operation* At = F.getDefiningOp() ? F.getDefiningOp() : Root;
        if (I == Vars.size())
          At->emitError("an action is only valid under ∧, ∨, ∃ or ite");
        else
          At->emitError() << getNextName(I) << " is read before it is assigned";
        Failed = true;
        return;
      }
    schlap::GuardOp::create(Builder, getLoc(F), materialize(F, Mapping));
  }

  mlir::Location getLoc(Value F) {
    if (Operation* Def = F.getDefiningOp())
      return Def->getLoc();
    return Root->getLoc();
  }

  // Assign the variable Target refers to from Source if that is possible.
  bool tryAssign(Value Target, Value Source, Paths& P, IRMapping& Mapping,
                 mlir::Location Loc) {
    std::optional<unsigned> I = getNextRef(Target);
    if (!I || isAssignedOnAll(*I, P) || readsUnassigned(Source, P))
      return false;
    schlap::AssignOp::create(Builder, Loc, Vars[*I].getResult(),
                             materialize(Source, Mapping));
    markAssigned(*I, P);
    return true;
  }

  // Run Fill in the body of a new region of Op and return its paths.
  template <typename Fn>
  void fillRegion(mlir::Region& Region, Fn Fill) {
    mlir::OpBuilder::InsertionGuard Guard(Builder);
    if (Region.empty())
      Region.emplaceBlock();
    Builder.setInsertionPointToEnd(&Region.front());
    Fill();
    schlap::DoneOp::create(Builder, Region.getParentOp()->getLoc());
  }

  // Remove duplicate paths.
  void unique(Paths& P) {
    Paths Result;
    for (Path& X : P)
      if (!llvm::is_contained(Result, X))
        Result.push_back(std::move(X));
    P = std::move(Result);
  }

  // Collect the disjuncts of a nest of ∨.
  void collectDisjuncts(Value F, llvm::SmallVectorImpl<Value>& Result) {
    if (auto Or = F.getDefiningOp<schlap::OrOp>()) {
      collectDisjuncts(Or.getLhs(), Result);
      collectDisjuncts(Or.getRhs(), Result);
      return;
    }
    Result.push_back(F);
  }

  void convert(Value F, Paths& P, IRMapping& Mapping) {
    if (Failed)
      return;
    Operation* Def = F.getDefiningOp();
    if (!isa_and_present<schlap::ActionRefOp>(Def) && !readsUnassigned(F, P))
      return guard(F, P, Mapping);

    mlir::Location Loc = getLoc(F);
    llvm::TypeSwitch<Operation*>(Def)
      .Case<schlap::AndOp>([&](schlap::AndOp And) {
        convert(And.getLhs(), P, Mapping);
        convert(And.getRhs(), P, Mapping);
      })
      .Case<schlap::OrOp>([&](schlap::OrOp) {
        llvm::SmallVector<Value> Disjuncts;
        collectDisjuncts(F, Disjuncts);
        auto Either = schlap::EitherOp::create(Builder, Loc, Disjuncts.size());
        Paths Result;
        for (auto [Region, Disjunct] : llvm::zip(Either.getBranches(),
                                                 Disjuncts)) {
          Paths Branch = P;
          IRMapping BranchMapping = Mapping;
          fillRegion(Region, [&] { convert(Disjunct, Branch, BranchMapping); });
          llvm::append_range(Result, Branch);
        }
        P = std::move(Result);
        unique(P);
      })
      .Case<schlap::ExistsOp>([&](schlap::ExistsOp Exists) {
        Value Domain = Exists.getDomain();
        if (readsUnassigned(Domain, P))
          return guard(Domain, P, Mapping);
        Value NewDomain = materialize(Domain, Mapping);
        auto Choose = schlap::ChooseOp::create(Builder, Loc, NewDomain);
        IRMapping BodyMapping = Mapping;
        fillRegion(Choose.getBody(), [&] {
          Value Arg = Exists.getBody().getArgument(0);
          BodyMapping.map(Arg, Choose.getBody().addArgument(Arg.getType(),
                                                            Arg.getLoc()));
          convert(getFormula(Exists), P, BodyMapping);
        });
      })
      .Case<schlap::EqOp>([&](schlap::EqOp Eq) {
        if (!tryAssign(Eq.getLhs(), Eq.getRhs(), P, Mapping, Loc) &&
            !tryAssign(Eq.getRhs(), Eq.getLhs(), P, Mapping, Loc))
          guard(F, P, Mapping);
      })
      .Case<schlap::MemberOp>([&](schlap::MemberOp Member) {
        std::optional<unsigned> I = getNextRef(Member.getLhs());
        Value Domain = Member.getRhs();
        if (!I || isAssignedOnAll(*I, P) || readsUnassigned(Domain, P))
          return guard(F, P, Mapping);
        Value NewDomain = materialize(Domain, Mapping);
        auto Choose = schlap::ChooseOp::create(Builder, Loc, NewDomain);
        fillRegion(Choose.getBody(), [&] {
          Value Arg = Choose.getBody().addArgument(Member.getLhs().getType(),
                                                   Loc);
          schlap::AssignOp::create(Builder, Loc, Vars[*I].getResult(), Arg);
        });
        markAssigned(*I, P);
      })
      .Case<schlap::IteOp>([&](schlap::IteOp Ite) {
        Value Cond = Ite.getCond();
        if (readsUnassigned(Cond, P))
          return guard(Cond, P, Mapping);
        auto Either = schlap::EitherOp::create(Builder, Loc, 2);
        Paths Then = P, Else = P;
        IRMapping ThenMapping = Mapping, ElseMapping = Mapping;
        fillRegion(Either.getBranches()[0], [&] {
          schlap::GuardOp::create(Builder, Loc,
                                  materialize(Cond, ThenMapping));
          convert(Ite.getLhs(), Then, ThenMapping);
        });
        fillRegion(Either.getBranches()[1], [&] {
          Value C = materialize(Cond, ElseMapping);
          Value Not = schlap::NotOp::create(Builder, Loc, C.getType(), C);
          schlap::GuardOp::create(Builder, Loc, Not);
          convert(Ite.getRhs(), Else, ElseMapping);
        });
        P = std::move(Then);
        llvm::append_range(P, Else);
        unique(P);
      })
      .Case<schlap::ActionRefOp>([&](schlap::ActionRefOp Ref) {
        schlap::ActionOp Action = lookupAction(Ref);
        Value Formula = Action ? getFormula(Action) : nullptr;
        if (!Formula) {
          Ref.emitError("expects an action: ") << Ref.getActionAttr();
          Failed = true;
          return;
        }
        if (!ActionStack.insert(Action).second) {
          Action.emitError("action refers to itself: ")
            << Action.getSymName();
          Failed = true;
          return;
        }
        auto InAction = schlap::InActionOp::create(Builder, Action.getLoc(),
                                                   Ref.getActionAttr());
        for (Path& X : P)
          if (!X.Action)
            X.Action = Action;
        IRMapping ActionMapping = Mapping;
        fillRegion(InAction.getBody(),
                   [&] { convert(Formula, P, ActionMapping); });
        ActionStack.erase(Action);
      })
      .Default([&](Operation*) { guard(F, P, Mapping); });
  }

  // Report the variables a path does not assign.
  void checkAssigned(Paths const& P) {
    llvm::SmallPtrSet<Operation*, 4> Reported;
    for (Path const& X : P) {
      llvm::BitVector Missing = X.Assigned;
      Missing.flip();
      if (Missing.none())
        continue;
      Operation* At = X.Action ? X.Action : Root;
      if (!Reported.insert(At).second)
        continue;
      unsigned I = *Missing.set_bits_begin();
      mlir::InFlightDiagnostic D = At->emitError();
      D << Vars[I].getName();
      if (IsInit)
        D << " has no initial value";
      else if (auto Action = dyn_cast_if_present<schlap::ActionOp>(X.Action))
        D << " has no next value in " << Action.getSymName();
      else
        D << " has no next value in next";
      Failed = true;
    }
  }
};

// Rewrite next-state relations into an executable form.
class NormalizeActionsPass
    : public schlap::impl::NormalizeActionsPassBase<NormalizeActionsPass> {
  using Base = schlap::impl::NormalizeActionsPassBase<NormalizeActionsPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::RewritePatternSet Patterns(&getContext());
    Patterns.add<RewriteNe, RewriteNotMember, RewriteUnchanged>(&getContext());
    mlir::FrozenRewritePatternSet Frozen(std::move(Patterns));

    bool Failed = false;
    getOperation()->walk([&](schlap::SpecOp Spec) {
      sinkDefinitions(Spec);
      mlir::walkAndApplyPatterns(Spec, Frozen);
      if (mlir::failed(checkPrimes(Spec))) {
        Failed = true;
        return;
      }
      for (Operation& Op : Spec.getBody().front())
        if (isa<schlap::InitOp, schlap::NextOp>(Op) &&
            mlir::failed(Converter(Spec, &Op).run()))
          Failed = true;
    });
    if (Failed)
      signalPassFailure();
  }
};
}  // namespace
