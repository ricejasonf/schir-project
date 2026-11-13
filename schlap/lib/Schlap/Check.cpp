#include <schlap/Check.h>
#include <schlap/Passes.h>
#include <schlap/Value.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/SymbolTable.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/TypeSwitch.h>
#include <vector>

namespace schlap {
#define GEN_PASS_DEF_CHECKPASS
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

using mlir::ArrayAttr;
using mlir::Attribute;
using mlir::Block;
using mlir::Operation;
using mlir::Value;

namespace {
// Hold the values of one branch of the evaluation of a formula.
struct Env {
  Env* Parent = nullptr;
  llvm::SmallDenseMap<Value, Attribute, 16> Values;
  // Hold the value of each variable in the state being built.
  llvm::SmallVector<Attribute, 4> Next;
  // Hold the action that the branch takes a step of.
  Operation* Action = nullptr;

  explicit Env(unsigned NumVars) : Next(NumVars) {}
  explicit Env(Env& Parent)
    : Parent(&Parent), Next(Parent.Next), Action(Parent.Action) {}

  Attribute lookup(Value V) const {
    for (Env const* E = this; E; E = E->Parent)
      if (Attribute A = E->Values.lookup(V))
        return A;
    return nullptr;
  }
};

// Select what an unprimed variable refers to.
enum class Mode { Init, Next, State };

// Search the reachable states of a specification breadth first.
class Checker {
public:
  Checker(schlap::SpecOp Spec, schlap::CheckOptions const& Options)
    : Spec(Spec), Options(Options), Context(Spec.getContext()),
      Symbols(Spec) {
    for (Operation& Op : Spec.getBody().front()) {
      if (auto Var = dyn_cast<schlap::VariableOp>(Op)) {
        VarIndex[Var.getResult()] = Vars.size();
        Vars.push_back(Var);
      } else if (auto I = dyn_cast<schlap::InitOp>(Op)) {
        Init = I;
      } else if (auto N = dyn_cast<schlap::NextOp>(Op)) {
        Next = N;
      } else if (auto Inv = dyn_cast<schlap::InvariantOp>(Op)) {
        Invariants.push_back(Inv);
      }
    }
  }

  llvm::FailureOr<schlap::CheckStats> run() {
    if (!Init || !Next)
      return Spec.emitError("expects a schlap.init and a schlap.next");
    for (Operation* Op : {Init.getOperation(), Next.getOperation()})
      if (!isa<schlap::DoneOp>(Op->getRegion(0).front().getTerminator()))
        return Op->emitError("expects a normalized specification "
                             "(run schlap-normalize-actions)");

    {
      Env Root(Vars.size());
      CurrentMode = Mode::Init;
      exec(Init.getBody().front(), Init.getBody().front().begin(), Root,
           [&](Env& E) { addState(E, nullptr, Init); });
    }

    for (size_t Head = 0; Head < Order.size() && !Failed; ++Head) {
      ArrayAttr S = Order[Head];
      CurrentMode = Mode::Next;
      Current = S.getValue();
      Expanding = S;
      uint64_t NumSuccessors = 0;
      Env Root(Vars.size());
      exec(Next.getBody().front(), Next.getBody().front().begin(), Root,
           [&](Env& E) {
             ++NumSuccessors;
             addState(E, S, E.Action ? E.Action : Next.getOperation());
           });
      if (!Failed && NumSuccessors == 0 && Options.CheckDeadlock) {
        mlir::InFlightDiagnostic D = Spec.emitError();
        D << "spec " << Spec.getSymName() << " deadlocks after "
          << printSteps(Seen.lookup(S).Depth);
        attachTrace(D, S);
        Failed = true;
      }
    }

    if (Failed)
      return mlir::failure();
    return schlap::CheckStats{Order.size(), MaxDepth};
  }

private:
  struct StateInfo {
    ArrayAttr Pred;
    Operation* Action = nullptr;
    uint64_t Depth = 0;
  };

  schlap::SpecOp Spec;
  schlap::CheckOptions const& Options;
  mlir::MLIRContext* Context;
  mlir::SymbolTable Symbols;
  llvm::SmallVector<schlap::VariableOp> Vars;
  llvm::DenseMap<Value, unsigned> VarIndex;
  schlap::InitOp Init;
  schlap::NextOp Next;
  llvm::SmallVector<schlap::InvariantOp> Invariants;

  llvm::DenseMap<Attribute, StateInfo> Seen;
  std::vector<ArrayAttr> Order;
  uint64_t MaxDepth = 0;
  bool Failed = false;

  Mode CurrentMode = Mode::Init;
  llvm::ArrayRef<Attribute> Current;
  ArrayAttr Expanding;

  static std::string printSteps(uint64_t N) {
    return std::to_string(N) + (N == 1 ? " step" : " steps");
  }

  // Print the variables of a state.
  void printState(llvm::raw_ostream& OS, ArrayAttr S) {
    for (auto [I, Var] : llvm::enumerate(Vars)) {
      if (I != 0)
        OS << ", ";
      OS << Var.getName() << " = ";
      schlap::printValue(OS, S[I], Var.getType());
    }
  }

  // Attach one note per state of the trace that ends in S.
  void attachTrace(mlir::InFlightDiagnostic& D, ArrayAttr S) {
    llvm::SmallVector<ArrayAttr> Trace;
    for (ArrayAttr X = S; X; X = Seen.lookup(X).Pred)
      Trace.push_back(X);
    for (auto [Step, X] : llvm::enumerate(llvm::reverse(Trace))) {
      Operation* Action = Seen.lookup(X).Action;
      std::string Text;
      llvm::raw_string_ostream OS(Text);
      if (Step == 0)
        OS << "initial state: ";
      else if (auto A = dyn_cast<schlap::ActionOp>(Action))
        OS << "step " << Step << ", " << A.getSymName() << ": ";
      else
        OS << "step " << Step << ": ";
      printState(OS, X);
      D.attachNote(Action->getLoc()) << Text;
    }
  }

  // Report an error at Op with the trace to the state being expanded.
  mlir::InFlightDiagnostic report(Operation* Op) {
    Failed = true;
    return Op->emitError();
  }

  void finishReport(mlir::InFlightDiagnostic& D) {
    if (Expanding)
      attachTrace(D, Expanding);
  }

  // Record a state built by a branch and check its invariants.
  void addState(Env& E, ArrayAttr Pred, Operation* Action) {
    if (Failed)
      return;
    for (auto [I, A] : llvm::enumerate(E.Next))
      if (!A) {
        mlir::InFlightDiagnostic D = report(Action);
        D << Vars[I].getName() << " has no value";
        finishReport(D);
        return;
      }
    ArrayAttr S = ArrayAttr::get(Context, E.Next);
    uint64_t Depth = Pred ? Seen.lookup(Pred).Depth + 1 : 0;
    if (!Seen.try_emplace(S, StateInfo{Pred, Action, Depth}).second)
      return;
    Order.push_back(S);
    MaxDepth = std::max(MaxDepth, Depth);
    checkInvariants(S);
    if (!Failed && Order.size() > Options.StateLimit) {
      Spec.emitError() << "spec " << Spec.getSymName()
                       << " exceeds the state limit of "
                       << Options.StateLimit << " states";
      Failed = true;
    }
  }

  // Check every invariant in state S.
  void checkInvariants(ArrayAttr S) {
    Mode SavedMode = CurrentMode;
    llvm::ArrayRef<Attribute> SavedCurrent = Current;
    ArrayAttr SavedExpanding = Expanding;
    CurrentMode = Mode::State;
    Current = S.getValue();
    Expanding = S;
    for (schlap::InvariantOp Inv : Invariants) {
      Env Root(Vars.size());
      auto Yield = cast<schlap::YieldOp>(
          Inv.getBody().front().getTerminator());
      Attribute A = eval(Yield.getValue(), Root);
      if (Failed)
        break;
      auto B = dyn_cast<mlir::BoolAttr>(A);
      if (B && B.getValue())
        continue;
      mlir::InFlightDiagnostic D = report(Inv);
      D << "invariant " << Inv.getSymName() << " is violated after "
        << printSteps(Seen.lookup(S).Depth);
      attachTrace(D, S);
      break;
    }
    CurrentMode = SavedMode;
    Current = SavedCurrent;
    Expanding = SavedExpanding;
  }

  // Return the message for an operation that does not fold.
  static llvm::StringRef getFoldError(Operation* Op) {
    if (isa<schlap::AddOp, schlap::SubOp, schlap::MulOp, schlap::NegOp>(Op))
      return "integer overflow";
    if (isa<schlap::ApplyOp, schlap::UpdateOp>(Op))
      return "the argument is not in the domain of the function";
    if (isa<schlap::RangeOp>(Op))
      return "the range is too large";
    if (isa<schlap::TupleGetOp>(Op))
      return "the index is out of range";
    return "";
  }

  // Evaluate the operation that defines V.
  Attribute evalOp(Operation* Op, Value V, Env& E) {
    if (auto Prime = dyn_cast<schlap::PrimeOp>(Op)) {
      Attribute A = E.Next[VarIndex.lookup(Prime.getVar())];
      if (!A) {
        mlir::InFlightDiagnostic D = report(Op);
        D << "′ " << Prime.getVar().getDefiningOp<schlap::VariableOp>()
                         .getName() << " has no value yet";
        finishReport(D);
      }
      return A;
    }
    if (isa<schlap::ForallOp, schlap::ExistsOp, schlap::FnOp>(Op))
      return evalBinder(Op, E);
    if (isa<schlap::ActionRefOp>(Op) || Op->getNumRegions() != 0) {
      mlir::InFlightDiagnostic D = report(Op);
      D << "unable to evaluate " << Op->getName();
      finishReport(D);
      return nullptr;
    }

    bool ShortCircuit = isa<schlap::AndOp, schlap::OrOp, schlap::ImpliesOp,
                            schlap::IteOp>(Op);
    unsigned NumOperands = Op->getNumOperands();
    llvm::SmallVector<Attribute, 4> Operands(NumOperands);
    llvm::SmallVector<mlir::OpFoldResult, 1> Results;
    bool Folded = false;
    for (unsigned I = 0; I != NumOperands && !Folded; ++I) {
      Operands[I] = eval(Op->getOperand(I), E);
      if (!Operands[I])
        return nullptr;
      if (ShortCircuit || I + 1 == NumOperands) {
        Results.clear();
        Folded = mlir::succeeded(Op->fold(Operands, Results)) &&
                 !Results.empty();
      }
    }
    if (NumOperands == 0)
      Folded = mlir::succeeded(Op->fold(Operands, Results)) &&
               !Results.empty();
    if (!Folded) {
      mlir::InFlightDiagnostic D = report(Op);
      llvm::StringRef Message = getFoldError(Op);
      if (Message.empty())
        D << "unable to evaluate " << Op->getName();
      else
        D << Message;
      finishReport(D);
      return nullptr;
    }

    mlir::OpFoldResult R = Results[cast<mlir::OpResult>(V).getResultNumber()];
    if (Attribute A = dyn_cast<Attribute>(R))
      return A;
    return eval(cast<Value>(R), E);
  }

  // Evaluate a quantifier or a function over a finite set.
  Attribute evalBinder(Operation* Op, Env& E) {
    auto Domain = dyn_cast_if_present<ArrayAttr>(eval(Op->getOperand(0), E));
    if (!Domain)
      return nullptr;
    Block& Body = Op->getRegion(0).front();
    Value Formula = cast<schlap::YieldOp>(Body.getTerminator()).getValue();
    bool IsForall = isa<schlap::ForallOp>(Op);
    llvm::SmallVector<Attribute> Pairs;
    for (Attribute Element : Domain) {
      Env Child(E);
      Child.Values[Body.getArgument(0)] = Element;
      Attribute A = eval(Formula, Child);
      if (!A)
        return nullptr;
      if (isa<schlap::FnOp>(Op)) {
        Pairs.push_back(ArrayAttr::get(Context, {Element, A}));
        continue;
      }
      auto B = dyn_cast<mlir::BoolAttr>(A);
      if (B && B.getValue() != IsForall)
        return B;
    }
    if (isa<schlap::FnOp>(Op))
      return ArrayAttr::get(Context, Pairs);
    return mlir::BoolAttr::get(Context, IsForall);
  }

  Attribute eval(Value V, Env& E) {
    if (Failed)
      return nullptr;
    auto It = VarIndex.find(V);
    if (It != VarIndex.end()) {
      if (CurrentMode != Mode::Init)
        return Current[It->second];
      Attribute A = E.Next[It->second];
      if (!A) {
        mlir::InFlightDiagnostic D = report(Vars[It->second]);
        D << Vars[It->second].getName() << " has no value yet";
      }
      return A;
    }
    if (Attribute A = E.lookup(V))
      return A;
    Operation* Op = V.getDefiningOp();
    if (!Op) {
      report(V.getParentBlock()->getParentOp()) << "unbound variable";
      return nullptr;
    }
    Attribute A = evalOp(Op, V, E);
    if (A)
      E.Values[V] = A;
    return A;
  }

  // Run the statements from It on and call K with each branch that
  // reaches the end of the block.
  void exec(Block& B, Block::iterator It, Env& E,
            llvm::function_ref<void(Env&)> K) {
    auto Rest = [&](Env& E2) { exec(B, std::next(It), E2, K); };
    for (; It != B.end(); ++It) {
      if (Failed)
        return;
      Operation* Op = &*It;
      if (auto Guard = dyn_cast<schlap::GuardOp>(Op)) {
        auto C = dyn_cast_if_present<mlir::BoolAttr>(
            eval(Guard.getCond(), E));
        if (!C || !C.getValue())
          return;
      } else if (auto Assign = dyn_cast<schlap::AssignOp>(Op)) {
        Attribute A = eval(Assign.getValue(), E);
        if (!A)
          return;
        Attribute& Slot = E.Next[VarIndex.lookup(Assign.getVar())];
        if (!Slot)
          Slot = A;
        else if (Slot != A)
          return;
      } else if (auto Either = dyn_cast<schlap::EitherOp>(Op)) {
        for (mlir::Region& Branch : Either.getBranches()) {
          Env Child(E);
          exec(Branch.front(), Branch.front().begin(), Child, Rest);
        }
        return;
      } else if (auto Choose = dyn_cast<schlap::ChooseOp>(Op)) {
        auto Domain = dyn_cast_if_present<ArrayAttr>(
            eval(Choose.getDomain(), E));
        if (!Domain)
          return;
        Block& Body = Choose.getBody().front();
        for (Attribute Element : Domain) {
          Env Child(E);
          Child.Values[Body.getArgument(0)] = Element;
          exec(Body, Body.begin(), Child, Rest);
        }
        return;
      } else if (auto InAction = dyn_cast<schlap::InActionOp>(Op)) {
        if (!E.Action)
          E.Action = Symbols.lookup(InAction.getAction());
        Block& Body = InAction.getBody().front();
        exec(Body, Body.begin(), E, Rest);
        return;
      } else if (isa<schlap::DoneOp>(Op)) {
        K(E);
        return;
      }
    }
  }
};

// Check the invariants of specifications.
class CheckPass
    : public schlap::impl::CheckPassBase<CheckPass> {
  using Base = schlap::impl::CheckPassBase<CheckPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    schlap::CheckOptions Options;
    Options.StateLimit = stateLimit;
    Options.CheckDeadlock = checkDeadlock;
    bool Failed = false;
    getOperation()->walk([&](schlap::SpecOp Spec) {
      llvm::FailureOr<schlap::CheckStats> Stats =
          schlap::checkSpec(Spec, Options);
      if (mlir::failed(Stats)) {
        Failed = true;
        return;
      }
      if (reportStates)
        Spec.emitRemark() << "spec " << Spec.getSymName() << " has "
                          << Stats->NumStates << " distinct states and depth "
                          << Stats->Depth;
    });
    if (Failed)
      signalPassFailure();
  }
};
}  // namespace

llvm::FailureOr<schlap::CheckStats>
schlap::checkSpec(SpecOp Spec, CheckOptions const& Options) {
  return Checker(Spec, Options).run();
}
