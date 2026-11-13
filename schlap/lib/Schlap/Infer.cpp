#include <schlap/Passes.h>
#include <mlir/IR/Diagnostics.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/TypeSwitch.h>

namespace schlap {
#define GEN_PASS_DEF_INFERTYPESPASS
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

using mlir::Operation;
using mlir::Type;
using mlir::Value;

namespace {
// Return true if T is or contains !schlap.unknown.
bool hasUnknown(Type T) {
  if (isa<schlap::UnknownType>(T))
    return true;
  if (auto ST = dyn_cast<schlap::SetType>(T))
    return hasUnknown(ST.getElementType());
  if (auto TT = dyn_cast<schlap::TupleType>(T))
    return llvm::any_of(TT.getElementTypes(), hasUnknown);
  if (auto FT = dyn_cast<schlap::FnType>(T))
    return hasUnknown(FT.getDomainType()) || hasUnknown(FT.getRangeType());
  return false;
}

// Return the most specific type that refines both or null.
Type unify(Type A, Type B) {
  if (A == B)
    return A;
  if (isa<schlap::UnknownType>(A))
    return B;
  if (isa<schlap::UnknownType>(B))
    return A;
  if (auto SA = dyn_cast<schlap::SetType>(A)) {
    auto SB = dyn_cast<schlap::SetType>(B);
    if (!SB)
      return {};
    Type E = unify(SA.getElementType(), SB.getElementType());
    return E ? schlap::SetType::get(A.getContext(), E) : Type();
  }
  if (auto TA = dyn_cast<schlap::TupleType>(A)) {
    auto TB = dyn_cast<schlap::TupleType>(B);
    if (!TB || TA.getElementTypes().size() != TB.getElementTypes().size())
      return {};
    llvm::SmallVector<Type> Elements;
    for (auto [EA, EB] : llvm::zip(TA.getElementTypes(),
                                   TB.getElementTypes())) {
      Type E = unify(EA, EB);
      if (!E)
        return {};
      Elements.push_back(E);
    }
    return schlap::TupleType::get(A.getContext(), Elements);
  }
  if (auto FA = dyn_cast<schlap::FnType>(A)) {
    auto FB = dyn_cast<schlap::FnType>(B);
    if (!FB)
      return {};
    Type D = unify(FA.getDomainType(), FB.getDomainType());
    Type R = unify(FA.getRangeType(), FB.getRangeType());
    return D && R ? schlap::FnType::get(A.getContext(), D, R) : Type();
  }
  return {};
}

// Print a type as the kind of value expected.
void printExpected(mlir::Diagnostic& D, Type T) {
  if (isa<schlap::UnknownType>(T))
    D << "a value";
  else if (auto ST = dyn_cast<schlap::SetType>(T);
           ST && isa<schlap::UnknownType>(ST.getElementType()))
    D << "a set";
  else if (auto FT = dyn_cast<schlap::FnType>(T);
           FT && hasUnknown(FT))
    D << "a function";
  else if (isa<schlap::PropType>(T))
    D << "a formula";
  else
    D << T;
}

// Return the notation of a Schlap operation for diagnostics.
llvm::StringRef getNotation(Operation* Op) {
  return llvm::TypeSwitch<Operation*, llvm::StringRef>(Op)
    .Case<schlap::AndOp>([](auto) { return "∧"; })
    .Case<schlap::OrOp>([](auto) { return "∨"; })
    .Case<schlap::NotOp>([](auto) { return "¬"; })
    .Case<schlap::ImpliesOp>([](auto) { return "⇒"; })
    .Case<schlap::IffOp>([](auto) { return "⇔"; })
    .Case<schlap::EqOp>([](auto) { return "="; })
    .Case<schlap::NeOp>([](auto) { return "≠"; })
    .Case<schlap::LtOp>([](auto) { return "<"; })
    .Case<schlap::LeOp>([](auto) { return "≤"; })
    .Case<schlap::GtOp>([](auto) { return ">"; })
    .Case<schlap::GeOp>([](auto) { return "≥"; })
    .Case<schlap::AddOp>([](auto) { return "+"; })
    .Case<schlap::SubOp>([](auto) { return "-"; })
    .Case<schlap::MulOp>([](auto) { return "*"; })
    .Case<schlap::NegOp>([](auto) { return "-"; })
    .Case<schlap::MemberOp>([](auto) { return "∈"; })
    .Case<schlap::NotMemberOp>([](auto) { return "∉"; })
    .Case<schlap::UnionOp>([](auto) { return "∪"; })
    .Case<schlap::IntersectOp>([](auto) { return "∩"; })
    .Case<schlap::SubsetOp>([](auto) { return "⊆"; })
    .Case<schlap::RangeOp>([](auto) { return "‥"; })
    .Case<schlap::ForallOp>([](auto) { return "∀"; })
    .Case<schlap::ExistsOp>([](auto) { return "∃"; })
    .Case<schlap::FnOp>([](auto) { return "↦"; })
    .Case<schlap::ApplyOp>([](auto) { return "@"; })
    .Case<schlap::UpdateOp>([](auto) { return "update"; })
    .Case<schlap::IteOp>([](auto) { return "ite"; })
    .Case<schlap::PrimeOp>([](auto) { return "′"; })
    .Case<schlap::UnchangedOp>([](auto) { return "unchanged"; })
    .Case<schlap::SetOfOp>([](auto) { return "set-of"; })
    .Case<schlap::TupleOp>([](auto) { return "tuple"; })
    .Case<schlap::TupleGetOp>([](auto) { return "tuple-ref"; })
    .Case<schlap::InitOp>([](auto) { return "init"; })
    .Case<schlap::ActionOp>([](auto) { return "action"; })
    .Case<schlap::NextOp>([](auto) { return "next"; })
    .Case<schlap::InvariantOp>([](auto) { return "invariant"; })
    .Default([](Operation* Op) { return Op->getName().getStringRef(); });
}

// Infer the types of the values of one specification.
class Inferrer {
public:
  explicit Inferrer(schlap::SpecOp Spec)
    : Spec(Spec),
      Unknown(schlap::UnknownType::get(Spec.getContext())),
      Prop(schlap::PropType::get(Spec.getContext())),
      Int(schlap::IntType::get(Spec.getContext())) {}

  llvm::LogicalResult run() {
    do {
      Changed = false;
      Spec.walk([&](Operation* Op) { visit(Op); });
    } while (Changed);
    if (Failed)
      return mlir::failure();
    checkInferred();
    return mlir::failure(Failed);
  }

private:
  schlap::SpecOp Spec;
  Type Unknown, Prop, Int;
  bool Changed = false;
  bool Failed = false;
  llvm::SmallPtrSet<Operation*, 8> Reported;

  Type getSet(Type T) { return schlap::SetType::get(T.getContext(), T); }

  // Return the element type of a set type or unknown.
  Type getElement(Type T) {
    if (auto ST = dyn_cast<schlap::SetType>(T))
      return ST.getElementType();
    return Unknown;
  }

  // Report a mismatch at Op once.
  mlir::InFlightDiagnostic report(Operation* Op) {
    Failed = true;
    if (!Reported.insert(Op).second)
      return mlir::InFlightDiagnostic();
    return Op->emitError();
  }

  // Refine the type of V by T or report that What has the wrong type.
  void refine(Value V, Type T, Operation* At, llvm::StringRef What) {
    Type U = unify(V.getType(), T);
    if (!U) {
      mlir::InFlightDiagnostic D = report(At);
      if (D.getUnderlyingDiagnostic()) {
        D << What << " of " << getNotation(At) << " must be ";
        printExpected(*D.getUnderlyingDiagnostic(), T);
        D << ", not " << V.getType();
      }
      return;
    }
    if (U != V.getType()) {
      V.setType(U);
      Changed = true;
    }
  }

  // Refine every value in Values to one common type.
  void refineAll(mlir::ValueRange Values, Type T, Operation* At,
                 llvm::StringRef What) {
    for (Value V : Values) {
      T = unify(T, V.getType());
      if (!T) {
        mlir::InFlightDiagnostic D = report(At);
        if (D.getUnderlyingDiagnostic()) {
          D << "the " << What << " of " << getNotation(At)
            << " must have one type, not";
          llvm::interleave(Values, [&](Value V) { D << ' ' << V.getType(); },
                           [&] { D << " and"; });
        }
        return;
      }
    }
    for (Value V : Values)
      refine(V, T, At, What);
  }

  // Refine the result of Op.
  void result(Operation* Op, Type T) {
    refine(Op->getResult(0), T, Op, "the result");
  }

  // Refine the yielded value and the bound variable of a binder.
  void binder(Operation* Op, Type BodyType) {
    Value Domain = Op->getOperand(0);
    refine(Domain, getSet(Unknown), Op, "the domain");
    mlir::Block& Body = Op->getRegion(0).front();
    if (Body.getNumArguments() == 1)
      refine(Body.getArgument(0), getElement(Domain.getType()), Op,
             "the bound variable");
    if (auto Yield = dyn_cast<schlap::YieldOp>(Body.getTerminator()))
      refine(Yield.getValue(), BodyType, Op, "the body");
  }

  void logic(Operation* Op) {
    for (Value V : Op->getOperands())
      refine(V, Prop, Op, "each operand");
    result(Op, Prop);
  }

  void arith(Operation* Op, Type ResultType) {
    for (Value V : Op->getOperands())
      refine(V, Int, Op, "each operand");
    result(Op, ResultType);
  }

  using Values = llvm::SmallVector<Value, 3>;

  void visit(Operation* Op) {
    llvm::TypeSwitch<Operation*>(Op)
      .Case<schlap::IntOp>([&](auto) { result(Op, Int); })
      .Case<schlap::TrueOp, schlap::FalseOp>([&](auto) { result(Op, Prop); })
      .Case<schlap::EmptySetOp>([&](auto) { result(Op, getSet(Unknown)); })
      .Case<schlap::AtomOp>([&](auto) {
        result(Op, schlap::AtomType::get(Op->getContext()));
      })
      .Case<schlap::AndOp, schlap::OrOp, schlap::NotOp, schlap::ImpliesOp,
            schlap::IffOp>([&](auto) { logic(Op); })
      .Case<schlap::EqOp, schlap::NeOp>([&](auto) {
        refineAll(Op->getOperands(), Unknown, Op, "operands");
        result(Op, Prop);
      })
      .Case<schlap::LtOp, schlap::LeOp, schlap::GtOp, schlap::GeOp>(
        [&](auto) { arith(Op, Prop); })
      .Case<schlap::AddOp, schlap::SubOp, schlap::MulOp, schlap::NegOp>(
        [&](auto) { arith(Op, Int); })
      .Case<schlap::MemberOp, schlap::NotMemberOp>([&](auto) {
        Value Lhs = Op->getOperand(0), Rhs = Op->getOperand(1);
        refine(Rhs, getSet(Lhs.getType()), Op, "the right operand");
        refine(Lhs, getElement(Rhs.getType()), Op, "the left operand");
        result(Op, Prop);
      })
      .Case<schlap::UnionOp, schlap::IntersectOp>([&](auto) {
        for (Value V : Op->getOperands())
          refine(V, getSet(Unknown), Op, "each operand");
        refineAll(Values{Op->getOperand(0), Op->getOperand(1),
                         Op->getResult(0)},
                  getSet(Unknown), Op, "operands");
      })
      .Case<schlap::SubsetOp>([&](auto) {
        for (Value V : Op->getOperands())
          refine(V, getSet(Unknown), Op, "each operand");
        refineAll(Op->getOperands(), Unknown, Op, "operands");
        result(Op, Prop);
      })
      .Case<schlap::RangeOp>([&](auto) { arith(Op, getSet(Int)); })
      .Case<schlap::SetOfOp>([&](schlap::SetOfOp SetOf) {
        refineAll(SetOf.getElements(), getElement(SetOf.getType()), Op,
                  "elements");
        Type E = SetOf.getElements().empty()
                   ? Unknown : SetOf.getElements().front().getType();
        result(Op, getSet(E));
      })
      .Case<schlap::TupleOp>([&](schlap::TupleOp Tuple) {
        if (auto TT = dyn_cast<schlap::TupleType>(Tuple.getType());
            TT && TT.getElementTypes().size() == Tuple.getNumOperands())
          for (auto [V, T] : llvm::zip(Tuple.getElements(),
                                       TT.getElementTypes()))
            refine(V, T, Op, "each element");
        llvm::SmallVector<Type> Types(Tuple.getElements().getTypes());
        result(Op, schlap::TupleType::get(Op->getContext(), Types));
      })
      .Case<schlap::TupleGetOp>([&](schlap::TupleGetOp Get) {
        Type T = Get.getTuple().getType();
        if (isa<schlap::UnknownType>(T))
          return;
        auto TT = dyn_cast<schlap::TupleType>(T);
        if (!TT) {
          mlir::InFlightDiagnostic D = report(Op);
          if (D.getUnderlyingDiagnostic())
            D << "the operand of tuple-ref must be a tuple, not " << T;
          return;
        }
        uint64_t Index = Get.getIndex();
        if (Index >= TT.getElementTypes().size()) {
          mlir::InFlightDiagnostic D = report(Op);
          if (D.getUnderlyingDiagnostic())
            D << "tuple-ref index " << Index << " is out of range for " << T;
          return;
        }
        result(Op, TT.getElementTypes()[Index]);
        llvm::SmallVector<Type> Types(TT.getElementTypes());
        Types[Index] = Get.getType();
        refine(Get.getTuple(), schlap::TupleType::get(Op->getContext(), Types),
               Op, "the operand");
      })
      .Case<schlap::ForallOp, schlap::ExistsOp>([&](auto) {
        binder(Op, Prop);
        result(Op, Prop);
      })
      .Case<schlap::FnOp>([&](schlap::FnOp Fn) {
        Type Range = Unknown;
        if (auto FT = dyn_cast<schlap::FnType>(Fn.getType()))
          Range = FT.getRangeType();
        binder(Op, Range);
        mlir::Block& Body = Fn.getBody().front();
        auto Yield = dyn_cast<schlap::YieldOp>(Body.getTerminator());
        if (!Yield || Body.getNumArguments() != 1)
          return;
        result(Op, schlap::FnType::get(Op->getContext(),
                                       Body.getArgument(0).getType(),
                                       Yield.getValue().getType()));
      })
      .Case<schlap::ApplyOp>([&](schlap::ApplyOp Apply) {
        Value Fn = Apply.getLhs(), Arg = Apply.getRhs();
        refine(Fn, schlap::FnType::get(Op->getContext(), Arg.getType(),
                                       Apply.getType()),
               Op, "the function");
        if (auto FT = dyn_cast<schlap::FnType>(Fn.getType())) {
          refine(Arg, FT.getDomainType(), Op, "the argument");
          result(Op, FT.getRangeType());
        }
      })
      .Case<schlap::UpdateOp>([&](schlap::UpdateOp Update) {
        Value Fn = Update.getFn();
        refine(Fn, schlap::FnType::get(Op->getContext(),
                                       Update.getArg().getType(),
                                       Update.getValue().getType()),
               Op, "the function");
        refine(Fn, Update.getType(), Op, "the function");
        if (auto FT = dyn_cast<schlap::FnType>(Fn.getType())) {
          refine(Update.getArg(), FT.getDomainType(), Op, "the argument");
          refine(Update.getValue(), FT.getRangeType(), Op, "the value");
          result(Op, FT);
        }
      })
      .Case<schlap::IteOp>([&](schlap::IteOp Ite) {
        refine(Ite.getCond(), Prop, Op, "the condition");
        refineAll(Values{Ite.getLhs(), Ite.getRhs(), Ite.getResult()},
                  Unknown, Op, "branches");
      })
      .Case<schlap::PrimeOp>([&](schlap::PrimeOp Prime) {
        if (!Prime.getVar().getDefiningOp<schlap::VariableOp>()) {
          mlir::InFlightDiagnostic D = report(Op);
          if (D.getUnderlyingDiagnostic())
            D << "′ applies only to a state variable";
          return;
        }
        refineAll(Values{Prime.getVar(), Prime.getResult()}, Unknown, Op,
                  "operand and result");
      })
      .Case<schlap::UnchangedOp, schlap::ActionRefOp>(
        [&](auto) { result(Op, Prop); })
      .Case<schlap::YieldOp>([&](schlap::YieldOp Yield) {
        Operation* Parent = Op->getParentOp();
        if (isa<schlap::InitOp, schlap::ActionOp, schlap::NextOp,
                schlap::InvariantOp>(Parent))
          refine(Yield.getValue(), Prop, Parent, "the formula");
      })
      .Case<schlap::GuardOp>([&](schlap::GuardOp Guard) {
        refine(Guard.getCond(), Prop, Op, "the condition");
      })
      .Case<schlap::AssignOp>([&](schlap::AssignOp Assign) {
        refineAll(Values{Assign.getVar(), Assign.getValue()}, Unknown, Op,
                  "operands");
      })
      .Case<schlap::ChooseOp>([&](schlap::ChooseOp Choose) {
        refine(Choose.getDomain(), getSet(Unknown), Op, "the domain");
        refine(Choose.getBody().getArgument(0),
               getElement(Choose.getDomain().getType()), Op,
               "the bound variable");
      });
  }

  // Report the variables whose type is still unknown, or else the values.
  void checkInferred() {
    for (auto Var : Spec.getBody().getOps<schlap::VariableOp>())
      if (hasUnknown(Var.getType())) {
        Var.emitError("unable to infer the type of variable ")
          << Var.getName();
        Failed = true;
      }
    if (Failed)
      return;
    Spec.walk([&](Operation* Op) {
      for (Value V : Op->getResults())
        if (isa<schlap::UnknownType>(V.getType())) {
          Op->emitError("unable to infer the type of the result of ")
            << getNotation(Op);
          Failed = true;
          return;
        }
    });
  }
};

// Infer the types of Schlap operations.
class InferTypesPass
    : public schlap::impl::InferTypesPassBase<InferTypesPass> {
  using Base = schlap::impl::InferTypesPassBase<InferTypesPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    bool Failed = false;
    getOperation()->walk([&](schlap::SpecOp Spec) {
      if (mlir::failed(Inferrer(Spec).run()))
        Failed = true;
    });
    if (Failed)
      signalPassFailure();
  }
};
}  // namespace
