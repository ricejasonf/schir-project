#include <schlap/ApplyRule.h>
#include <schlap/Passes.h>
#include <schir_common/Interfaces.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/SymbolTable.h>
#include <mlir/Transforms/RegionUtils.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringSet.h>
#include <optional>
#include <string>
#include <vector>

namespace schlap {
#define GEN_PASS_DEF_APPLYRULEPASS
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

using mlir::Block;
using mlir::IRMapping;
using mlir::Operation;
using mlir::Value;

namespace {
// Describe an operation of the function using or mutating a value.
struct Event {
  bool IsMutation = false;
  Value V;
  // Hold the schlap.on_use or schlap.on_mutate of the rule.
  Operation* Handler = nullptr;
};

// Describe one step of a path through the function.
struct Step {
  Operation* Op = nullptr;
  // Hold the requirements checked before the step.
  llvm::SmallVector<std::pair<Event, schlap::RequireOp>, 2> Requires;
  // Hold the event whose action the step takes.
  std::optional<Event> Action;
  // Mark the last step of the operation, which leaves it or enters a
  // region.
  bool IsLast = false;
  // Hold each position after the step and the name of its action.
  llvm::SmallVector<std::pair<unsigned, std::string>, 2> Targets;
};

// Build the specification of the paths through a function that a rule
// states.
class RuleApplier {
public:
  RuleApplier(schlap::RuleOp Rule, Operation* Fn, mlir::OpBuilder& Builder)
    : Rule(Rule), Fn(Fn), Builder(Builder),
      Unknown(schlap::UnknownType::get(Rule.getContext())) {}

  llvm::FailureOr<schlap::SpecOp> run() {
    if (mlir::failed(findHandlers()))
      return mlir::failure();
    mlir::Region* BodyRegion = schlap::getRuleBody(Fn);
    if (!BodyRegion)
      return Fn->emitError("expects a function with a body");
    mlir::Region& Body = *BodyRegion;
    if (!Body.hasOneBlock())
      return Fn->emitError("expects a function with one block");
    for (Value Arg : Body.getArguments())
      number(Arg, nullptr);
    layout(Body.front());
    if (Failed)
      return mlir::failure();
    End = Steps.size();
    link(Body.front(), End);
    schlap::SpecOp Spec = build();
    if (Failed) {
      Spec.erase();
      return mlir::failure();
    }
    return Spec;
  }

private:
  schlap::RuleOp Rule;
  Operation* Fn;
  mlir::OpBuilder& Builder;
  mlir::Type Unknown;
  schlap::OnUseOp OnUse;
  schlap::OnMutateOp OnMutate;
  bool Failed = false;

  llvm::DenseMap<Value, unsigned> ValueIds;
  llvm::SmallVector<Value> Values;
  llvm::StringMap<Value> ValuesByName;
  llvm::DenseMap<Value, Value> ReadFrom;
  llvm::DenseMap<Operation*, unsigned> OpIds;
  llvm::DenseMap<Operation*, unsigned> FirstStep;
  std::vector<Step> Steps;
  unsigned End = 0;

  IRMapping RuleMapping;
  llvm::SmallVector<Value> RuleVars;
  Value Pc;
  llvm::StringSet<> SymbolNames;

  // Find the event handlers of the rule.
  llvm::LogicalResult findHandlers() {
    for (Operation& Op : Rule.getBody().front()) {
      if (auto Use = dyn_cast<schlap::OnUseOp>(Op)) {
        if (OnUse)
          return Use.emitError("expects at most one schlap.on_use");
        OnUse = Use;
      } else if (auto Mutate = dyn_cast<schlap::OnMutateOp>(Op)) {
        if (OnMutate)
          return Mutate.emitError("expects at most one schlap.on_mutate");
        OnMutate = Mutate;
      }
    }
    return mlir::success();
  }

  std::string getValueName(Value V) {
    return "v" + std::to_string(ValueIds.lookup(V));
  }

  std::string getOpName(Operation* Op) {
    return Op->getName().getStringRef().str() + " #" +
           std::to_string(OpIds.lookup(Op));
  }

  // Number a value of the function and record what it is read from.
  void number(Value V, Operation* Owner) {
    ValueIds[V] = Values.size();
    Values.push_back(V);
    ValuesByName[getValueName(V)] = V;
    auto Access = dyn_cast_if_present<schir_common::ValueAccess>(Owner);
    if (!Access)
      return;
    Value From = Access.getReadFrom(V);
    if (From && ValueIds.count(From))
      ReadFrom[V] = From;
  }

  // Return the value that V is read from, followed back to a value that
  // is not read from another.
  Value getRoot(Value V) {
    for (size_t I = 0; I <= Values.size(); ++I) {
      auto It = ReadFrom.find(V);
      if (It == ReadFrom.end())
        return V;
      V = It->second;
    }
    return V;
  }

  // Return the values read from Root that are defined before Op.
  llvm::SmallVector<Value> getReadFrom(Value Root, Operation* Op) {
    llvm::SmallVector<Value> Result;
    auto Add = [&](Value V) {
      if (V != Root && getRoot(V) == Root)
        Result.push_back(V);
    };
    for (Operation* Cur = Op; Cur && Cur != Fn;
         Cur = Cur->getParentOp()) {
      Block* B = Cur->getBlock();
      for (Value Arg : B->getArguments())
        Add(Arg);
      for (Operation& Prev : llvm::make_range(B->begin(), Cur->getIterator()))
        for (Value V : Prev.getResults())
          Add(V);
    }
    llvm::sort(Result, [&](Value A, Value B) {
      return ValueIds.lookup(A) < ValueIds.lookup(B);
    });
    return Result;
  }

  // Return the events of an operation: its uses and then its mutations.
  llvm::SmallVector<Event> getEvents(Operation* Op) {
    llvm::SmallVector<Event> Events;
    auto Access = dyn_cast<schir_common::ValueAccess>(Op);
    for (mlir::OpOperand& Operand : Op->getOpOperands())
      if (OnUse && (!Access || Access.isUsed(Operand)))
        Events.push_back({false, Operand.get(), OnUse});
    if (Access && OnMutate)
      for (mlir::OpOperand& Operand : Op->getOpOperands())
        if (Access.isMutated(Operand))
          Events.push_back({true, Operand.get(), OnMutate});
    return Events;
  }

  // Number the values and create the steps of the operations of a block.
  void layout(Block& B) {
    for (Operation& Op : B) {
      OpIds[&Op] = OpIds.size();
      for (Value V : Op.getResults())
        number(V, &Op);
      FirstStep[&Op] = Steps.size();
      llvm::SmallVector<std::pair<Event, schlap::RequireOp>, 2> Pending;
      for (Event& E : getEvents(&Op)) {
        Block& Handler = E.Handler->getRegion(0).front();
        for (auto Require : Handler.getOps<schlap::RequireOp>())
          Pending.push_back({E, Require});
        if (!isa<schlap::YieldOp>(Handler.back()))
          continue;
        Step S;
        S.Op = &Op;
        S.Requires = std::move(Pending);
        S.Action = E;
        Steps.push_back(std::move(S));
        Pending.clear();
      }
      bool HasRegions = llvm::any_of(Op.getRegions(), [](mlir::Region& R) {
        return !R.empty();
      });
      if (HasRegions || !Pending.empty() ||
          Steps.size() == FirstStep[&Op]) {
        Step S;
        S.Op = &Op;
        S.Requires = std::move(Pending);
        Steps.push_back(std::move(S));
      }
      Steps.back().IsLast = true;
      for (mlir::Region& R : Op.getRegions()) {
        if (R.empty())
          continue;
        if (!R.hasOneBlock()) {
          Op.emitError("expects regions with one block");
          Failed = true;
          return;
        }
        for (Value Arg : R.getArguments())
          number(Arg, &Op);
        layout(R.front());
        if (Failed)
          return;
      }
    }
  }

  // Name the values in a list.
  std::string getValueNames(mlir::ValueRange Vs) {
    std::string Names;
    for (Value V : Vs) {
      Names += Names.empty() ? " defines " : ", ";
      Names += getValueName(V);
    }
    return Names;
  }

  // Set the positions after the steps of the operations of a block that
  // continues at Cont.
  void link(Block& B, unsigned Cont) {
    for (auto It = B.begin(); It != B.end(); ++It) {
      Operation& Op = *It;
      auto Next = std::next(It);
      unsigned After = Next == B.end() ? Cont : FirstStep.lookup(&*Next);
      for (unsigned P = FirstStep.lookup(&Op);
           P < Steps.size() && Steps[P].Op == &Op; ++P) {
        Step& S = Steps[P];
        std::string Name = getOpName(&Op);
        if (S.Action)
          Name += (S.Action->IsMutation ? " mutates " : " uses ") +
                  getValueName(S.Action->V);
        if (!S.IsLast) {
          S.Targets.push_back({P + 1, Name});
          continue;
        }
        unsigned NumRegions = llvm::count_if(Op.getRegions(),
          [](mlir::Region& R) { return !R.empty(); });
        if (NumRegions == 0) {
          S.Targets.push_back({After, Name + getValueNames(Op.getResults())});
          continue;
        }
        for (auto [I, R] : llvm::enumerate(Op.getRegions())) {
          if (R.empty())
            continue;
          Block& Entry = R.front();
          unsigned Target = Entry.empty() ? After
                                          : FirstStep.lookup(&Entry.front());
          std::string RegionName = Name;
          if (NumRegions > 1)
            RegionName += " region " + std::to_string(I);
          S.Targets.push_back(
              {Target, RegionName + getValueNames(Entry.getArguments())});
          link(Entry, After);
        }
      }
    }
  }

  // Return a symbol name not used in the specification yet.
  std::string getUniqueName(std::string Name) {
    std::string Unique = Name;
    for (unsigned I = 2; !SymbolNames.insert(Unique).second; ++I)
      Unique = Name + " (" + std::to_string(I) + ")";
    return Unique;
  }

  // Create the operations of formulas in one region.
  struct RegionBuilder {
    RuleApplier& Applier;
    mlir::OpBuilder B;
    mlir::Location Loc;
    IRMapping Mapping;
    llvm::DenseMap<Value, Value> Atoms;

    RegionBuilder(RuleApplier& Applier, mlir::Region& Region,
                  mlir::Location Loc)
      : Applier(Applier), B(Applier.Rule.getContext()), Loc(Loc),
        Mapping(Applier.RuleMapping) {
      B.setInsertionPointToEnd(&Region.emplaceBlock());
    }

    mlir::Type unknown() { return Applier.Unknown; }

    Value atom(Value V) {
      Value& A = Atoms[V];
      if (!A)
        A = schlap::AtomOp::create(B, Loc, unknown(),
                                   Applier.getValueName(V));
      return A;
    }

    Value integer(unsigned N) {
      return schlap::IntOp::create(B, Loc, unknown(), N);
    }

    Value eq(Value A, Value C) {
      return schlap::EqOp::create(B, Loc, unknown(), A, C);
    }

    Value conj(Value A, Value C) {
      return schlap::AndOp::create(B, Loc, unknown(), A, C);
    }

    Value pcIs(unsigned P) { return eq(Applier.Pc, integer(P)); }

    Value nextPcIs(unsigned P) {
      Value Prime = schlap::PrimeOp::create(B, Loc, unknown(), Applier.Pc);
      return eq(Prime, integer(P));
    }

    // Clone the definition of V from the rule and what it uses.
    Value clone(Value V) {
      if (Value Mapped = Mapping.lookupOrNull(V))
        return Mapped;
      Operation* Def = V.getDefiningOp();
      if (!Def)
        return V;
      for (Value Operand : Def->getOperands())
        clone(Operand);
      llvm::SetVector<Value> Above;
      mlir::getUsedValuesDefinedAbove(Def->getRegions(), Above);
      for (Value Operand : Above)
        clone(Operand);
      B.clone(*Def, Mapping);
      return Mapping.lookup(V);
    }

    // Clone the value of an event handler for the value of the event.
    Value cloneForEvent(Value V, Event const& E) {
      Value Arg = E.Handler->getRegion(0).getArgument(0);
      Mapping.map(Arg, atom(E.V));
      return clone(V);
    }

    void yield(Value F) { schlap::YieldOp::create(B, Loc, F); }
  };

  // Replace each root and read_from of a region with a constant.
  void resolveConstants(mlir::Region& Region, Operation* Context) {
    Region.walk<mlir::WalkOrder::PostOrder>([&](Operation* Op) {
      if (!isa<schlap::RootOp, schlap::ReadFromOp>(Op))
        return;
      auto Atom = Op->getOperand(0).getDefiningOp<schlap::AtomOp>();
      Value V = Atom ? ValuesByName.lookup(Atom.getName()) : Value();
      if (!V) {
        Op->emitError("expects a value of the function");
        Failed = true;
        return;
      }
      mlir::OpBuilder B(Op);
      Value Result;
      if (isa<schlap::RootOp>(Op)) {
        Result = schlap::AtomOp::create(B, Op->getLoc(), Unknown,
                                        getValueName(getRoot(V)));
      } else {
        llvm::SmallVector<Value> Elements;
        for (Value X : getReadFrom(V, Context))
          Elements.push_back(schlap::AtomOp::create(B, Op->getLoc(), Unknown,
                                                    getValueName(X)));
        Result = schlap::SetOfOp::create(B, Op->getLoc(), Unknown, Elements);
      }
      Op->getResult(0).replaceAllUsesWith(Result);
      Op->erase();
    });
  }

  // Create the specification of the steps.
  schlap::SpecOp build() {
    mlir::Location FnLoc = Fn->getLoc();
    std::string Name = (Rule.getSymName() + "(" +
                        mlir::SymbolTable::getSymbolName(Fn).getValue() +
                        ")").str();
    auto Spec = schlap::SpecOp::create(Builder, FnLoc, Name);
    mlir::OpBuilder B = mlir::OpBuilder::atBlockEnd(
        &Spec.getBody().emplaceBlock());

    // Copy the variables and definitions of the rule.
    schlap::InitOp RuleInit;
    for (Operation& Op : Rule.getBody().front()) {
      if (auto Var = dyn_cast<schlap::VariableOp>(Op)) {
        if (Var.getName() == "pc") {
          Var.emitError("expects a variable not named pc");
          Failed = true;
        }
        auto New = schlap::VariableOp::create(B, Var.getLoc(), Var.getType(),
                                              Var.getName());
        RuleMapping.map(Var.getResult(), New.getResult());
        RuleVars.push_back(New);
      } else if (auto Init = dyn_cast<schlap::InitOp>(Op)) {
        RuleInit = Init;
      } else if (!isa<schlap::OnUseOp, schlap::OnMutateOp>(Op)) {
        B.clone(Op, RuleMapping);
      }
    }
    Pc = schlap::VariableOp::create(B, FnLoc, Unknown, "pc");

    // State the initial condition of the rule and the first step.
    {
      auto Init = schlap::InitOp::create(B, FnLoc);
      RegionBuilder R(*this, Init.getBody(), FnLoc);
      Value F = R.pcIs(0);
      if (RuleInit) {
        Block& Body = RuleInit.getBody().front();
        if (auto Yield = dyn_cast<schlap::YieldOp>(Body.getTerminator()))
          F = R.conj(R.clone(Yield.getValue()), F);
      }
      R.yield(F);
    }

    // Create one action per step and target.
    llvm::SmallVector<std::pair<std::string, mlir::Location>> Actions;
    for (auto [P, S] : llvm::enumerate(Steps)) {
      for (auto& [Target, ActionName] : S.Targets) {
        mlir::Location Loc = S.Op->getLoc();
        std::string Symbol = getUniqueName(ActionName);
        auto Action = schlap::ActionOp::create(B, Loc, Symbol);
        RegionBuilder R(*this, Action.getBody(), Loc);
        Value At = R.pcIs(P);
        Value Next = R.nextPcIs(Target);
        Value F = R.conj(At, Next);
        Value Effect;
        if (S.Action) {
          auto Yield = cast<schlap::YieldOp>(
              S.Action->Handler->getRegion(0).front().back());
          Effect = R.cloneForEvent(Yield.getValue(), *S.Action);
        } else if (!RuleVars.empty()) {
          Effect = schlap::UnchangedOp::create(R.B, Loc, Unknown, RuleVars);
        }
        if (Effect)
          F = R.conj(F, Effect);
        R.yield(F);
        resolveConstants(Action.getBody(), S.Op);
        Actions.push_back({Symbol, Loc});
      }
    }

    // Stay at the end of the function.
    {
      std::string Symbol = getUniqueName("end");
      auto Action = schlap::ActionOp::create(B, FnLoc, Symbol);
      RegionBuilder R(*this, Action.getBody(), FnLoc);
      llvm::SmallVector<Value> Vars(RuleVars);
      Vars.push_back(Pc);
      Value AtEnd = R.pcIs(End);
      Value Unchanged =
          schlap::UnchangedOp::create(R.B, FnLoc, Unknown, Vars);
      R.yield(R.conj(AtEnd, Unchanged));
      Actions.push_back({Symbol, FnLoc});
    }

    // Take one of the actions in each step.
    {
      auto Next = schlap::NextOp::create(B, FnLoc);
      RegionBuilder R(*this, Next.getBody(), FnLoc);
      Value F;
      for (auto& [Symbol, Loc] : Actions) {
        Value Ref = schlap::ActionRefOp::create(R.B, Loc, Unknown, Symbol);
        F = F ? schlap::OrOp::create(R.B, FnLoc, Unknown, F, Ref) : Ref;
      }
      R.yield(F);
    }

    // Require each requirement where its step is about to run.
    for (auto [P, S] : llvm::enumerate(Steps)) {
      for (auto& [E, Require] : S.Requires) {
        mlir::Location Loc = S.Op->getLoc();
        std::string Symbol = getUniqueName(
            (E.IsMutation ? "mutation of " : "use of ") + getValueName(E.V) +
            " by " + getOpName(S.Op));
        auto Invariant = schlap::InvariantOp::create(B, Loc, Symbol);
        RegionBuilder R(*this, Invariant.getBody(), Loc);
        Value Cond = R.cloneForEvent(Require.getCond(), E);
        R.yield(schlap::ImpliesOp::create(R.B, Loc, Unknown, R.pcIs(P), Cond));
        resolveConstants(Invariant.getBody(), S.Op);
      }
    }
    return Spec;
  }
};

// Apply each rule of a module to each function of the module.
class ApplyRulePass
    : public schlap::impl::ApplyRulePassBase<ApplyRulePass> {
  using Base = schlap::impl::ApplyRulePassBase<ApplyRulePass>;

public:
  using Base::Base;

  void runOnOperation() override {
    mlir::ModuleOp Module = getOperation();
    llvm::SmallVector<schlap::RuleOp> Rules(Module.getOps<schlap::RuleOp>());
    llvm::SmallVector<Operation*> Fns;
    for (Operation& Op : Module.getOps())
      if (schlap::getRuleBody(&Op))
        Fns.push_back(&Op);
    mlir::OpBuilder Builder = mlir::OpBuilder::atBlockEnd(Module.getBody());
    bool Failed = false;
    for (schlap::RuleOp Rule : Rules)
      for (Operation* Fn : Fns)
        if (mlir::failed(schlap::applyRule(Rule, Fn, Builder)))
          Failed = true;
    if (Failed)
      signalPassFailure();
  }
};
}  // namespace

mlir::Region* schlap::getRuleBody(Operation* Op) {
  if (auto Fn = dyn_cast<mlir::FunctionOpInterface>(Op))
    return Fn.isExternal() ? nullptr : &Fn.getFunctionBody();
  if (!isa<mlir::SymbolOpInterface>(Op) || isa<mlir::ModuleOp>(Op) ||
      isa<schlap::SchlapDialect>(Op->getDialect()) ||
      Op->getNumRegions() != 1 || Op->getRegion(0).empty())
    return nullptr;
  return &Op->getRegion(0);
}

llvm::FailureOr<schlap::SpecOp>
schlap::applyRule(RuleOp Rule, Operation* Fn, mlir::OpBuilder& Builder) {
  return RuleApplier(Rule, Fn, Builder).run();
}
