#include <schlap/Kernel.h>
#include <schlap/Passes.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/IR/SymbolTable.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/ScopeExit.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <string>

namespace schlap {
#define GEN_PASS_DEF_CHECKPROOFSPASS
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

using mlir::failure;
using mlir::FailureOr;
using mlir::Location;
using mlir::LogicalResult;
using mlir::Operation;
using mlir::success;
using mlir::Value;

// Verify that each region has one block with NumArgs arguments that ends
// with schlap.yield.
static LogicalResult verifyYieldRegions(Operation* Op, unsigned NumArgs) {
  for (mlir::Region& Region : Op->getRegions()) {
    mlir::Block& Block = Region.front();
    if (Block.getNumArguments() != NumArgs)
      return Op->emitOpError("expects ") << NumArgs
                                          << " bound variables per region";
    if (Block.empty() || !isa<schlap::YieldOp>(Block.back()))
      return Op->emitOpError("expects each region to end with schlap.yield");
  }
  return success();
}

LogicalResult schlap::TheoremOp::verifyRegions() {
  return verifyYieldRegions(*this, 0);
}

LogicalResult schlap::DefinitionOp::verifyRegions() {
  return verifyYieldRegions(*this, 0);
}

LogicalResult schlap::AxiomOp::verifyRegions() {
  return verifyYieldRegions(*this, 0);
}

LogicalResult schlap::LambdaOp::verifyRegions() {
  return verifyYieldRegions(*this, 1);
}

// Return the value yielded by a block.
static Value yielded(mlir::Block& Block) {
  return cast<schlap::YieldOp>(Block.back()).getValue();
}

// Return the value yielded by the body of a binder.
static Value yielded(Operation* Binder) {
  return yielded(Binder->getRegion(0).front());
}

// Return true if V depends on the variable X.
static bool dependsOn(Value V, Value X, llvm::DenseSet<Value>& Visited) {
  if (V == X)
    return true;
  if (isa<mlir::BlockArgument>(V) || !Visited.insert(V).second)
    return false;
  Operation* Op = V.getDefiningOp();
  bool Found = false;
  Op->walk([&](Operation* Inner) {
    for (Value Operand : Inner->getOperands())
      if (dependsOn(Operand, X, Visited)) {
        Found = true;
        return mlir::WalkResult::interrupt();
      }
    return mlir::WalkResult::advance();
  });
  return Found;
}

static bool dependsOn(Value V, Value X) {
  llvm::DenseSet<Value> Visited;
  return dependsOn(V, X, Visited);
}

namespace {
// Print terms in the surface notation.
class Printer {
  enum Prec { Binder, Arrow, Conn, Cmp, App, Atom };

  schlap::VarNameFn VarName;
  llvm::DenseMap<Value, std::string> Names;
  llvm::StringSet<> Used;

public:
  Printer(schlap::VarNameFn VarName) : VarName(VarName) {}

  std::string str(Value V) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    print(OS, V, Binder);
    return Str;
  }

private:
  // Return a name for a variable that is unique in this printer.
  llvm::StringRef name(Value Var) {
    auto [It, Inserted] = Names.try_emplace(Var);
    if (!Inserted)
      return It->second;
    std::string Base = VarName(Var.getLoc());
    if (Base.empty())
      Base = "x";
    std::string Name = Base;
    for (unsigned I = 1; !Used.insert(Name).second; ++I)
      Name = Base + std::to_string(I);
    It->second = Name;
    return It->second;
  }

  // Free the name of a bound variable at the end of its scope.
  void unbind(Value Var) {
    auto It = Names.find(Var);
    if (It == Names.end())
      return;
    Used.erase(It->second);
    Names.erase(It);
  }

  // Return the surface name of a kernel operation applied to arguments.
  static llvm::StringRef surfaceName(Operation* Op) {
    return llvm::TypeSwitch<Operation*, llvm::StringRef>(Op)
        .Case([](schlap::AndIntroOp) { return "∧-intro"; })
        .Case([](schlap::AndLeftOp) { return "∧-left"; })
        .Case([](schlap::AndRightOp) { return "∧-right"; })
        .Case([](schlap::OrInlOp) { return "∨-inl"; })
        .Case([](schlap::OrInrOp) { return "∨-inr"; })
        .Case([](schlap::OrElimOp) { return "∨-elim"; })
        .Case([](schlap::FalseElimOp) { return "⊥-elim"; })
        .Case([](schlap::ExistsIntroOp) { return "∃-intro"; })
        .Case([](schlap::ExistsElimOp) { return "∃-elim"; })
        .Case([](schlap::SubstOp) { return "subst"; })
        .Case([](schlap::NatRecOp) { return "ℕ-rec"; })
        .Case([](schlap::SuccOp) { return "succ"; })
        .Default([](Operation* Op) {
          return Op->getName().stripDialect();
        });
  }

  void print(llvm::raw_ostream& OS, Value V, int Min) {
    if (isa<mlir::BlockArgument>(V)) {
      OS << name(V);
      return;
    }
    Operation* Op = V.getDefiningOp();
    // Print with parentheses when the form binds looser than Min.
    auto Wrap = [&](int P, auto Fn) {
      if (P < Min)
        OS << '(';
      Fn();
      if (P < Min)
        OS << ')';
    };
    auto Infix = [&](int P, llvm::StringRef Sym, int L, int R) {
      Wrap(P, [&] {
        print(OS, Op->getOperand(0), L);
        OS << ' ' << Sym << ' ';
        print(OS, Op->getOperand(1), R);
      });
    };
    auto Binding = [&](llvm::StringRef Sym, bool Domain) {
      Value Var = Op->getRegion(0).front().getArgument(0);
      Wrap(Binder, [&] {
        OS << Sym << ' ' << name(Var);
        if (Domain) {
          OS << " : ";
          print(OS, Op->getOperand(0), App);
        }
        OS << ", ";
        print(OS, yielded(Op), Binder);
        unbind(Var);
      });
    };
    // Print a conjunction or disjunction, folding the same one on the left.
    auto Conn = [&](llvm::StringRef Sym) {
      Wrap(Prec::Conn, [&] {
        Value Lhs = Op->getOperand(0);
        bool Same = Lhs.getDefiningOp() &&
                    Lhs.getDefiningOp()->getName() == Op->getName();
        print(OS, Lhs, Same ? Prec::Conn : Cmp);
        OS << ' ' << Sym << ' ';
        print(OS, Op->getOperand(1), Cmp);
      });
    };
    llvm::TypeSwitch<Operation*>(Op)
        .Case<schlap::PiOp, schlap::ForallOp>([&](Operation* Op) {
          Value Var = Op->getRegion(0).front().getArgument(0);
          Value Body = yielded(Op);
          if (dependsOn(Body, Var))
            return Binding("∀", true);
          if (isa_and_nonnull<schlap::FalseOp>(Body.getDefiningOp()))
            return Wrap(App, [&] {
              OS << "¬";
              print(OS, Op->getOperand(0), Atom);
            });
          Wrap(Arrow, [&] {
            print(OS, Op->getOperand(0), Prec::Conn);
            OS << " ⇒ ";
            print(OS, Body, Arrow);
          });
        })
        .Case([&](schlap::ExistsOp) { Binding("∃", true); })
        .Case([&](schlap::LambdaOp) { Binding("λ", false); })
        .Case([&](schlap::ImpliesOp) { Infix(Arrow, "⇒", Prec::Conn, Arrow); })
        .Case([&](schlap::IffOp) { Infix(Binder, "⇔", Arrow, Arrow); })
        .Case([&](schlap::AndOp) { Conn("∧"); })
        .Case([&](schlap::OrOp) { Conn("∨"); })
        .Case([&](schlap::EqOp) { Infix(Cmp, "=", App, App); })
        .Case([&](schlap::NeOp) { Infix(Cmp, "≠", App, App); })
        .Case([&](schlap::NotOp) {
          Wrap(App, [&] {
            OS << "¬";
            print(OS, Op->getOperand(0), Atom);
          });
        })
        .Case([&](schlap::TrueOp) { OS << "⊤"; })
        .Case([&](schlap::FalseOp) { OS << "⊥"; })
        .Case([&](schlap::PropSortOp) { OS << "Prop"; })
        .Case([&](schlap::TypeSortOp) { OS << "Type"; })
        .Case([&](schlap::NatOp) { OS << "ℕ"; })
        .Case([&](schlap::ZeroOp) { OS << "zero"; })
        .Case([&](schlap::TopIntroOp) { OS << "⊤-intro"; })
        .Case([&](schlap::ReflOp) { OS << "refl"; })
        .Case([&](schlap::IntOp Int) { OS << Int.getValue(); })
        .Case([&](schlap::RefOp Ref) { OS << Ref.getName(); })
        .Case([&](schlap::AppOp App) {
          Wrap(Prec::App, [&] {
            print(OS, App.getLhs(), Prec::App);
            OS << ' ';
            print(OS, App.getRhs(), Atom);
          });
        })
        .Default([&](Operation* Op) {
          Wrap(App, [&] {
            OS << surfaceName(Op);
            for (Value Operand : Op->getOperands()) {
              OS << ' ';
              print(OS, Operand, Atom);
            }
          });
        });
  }
};

// Return the options of every greedy rewrite in the kernel.
mlir::GreedyRewriteConfig kernelRewriteConfig() {
  mlir::GreedyRewriteConfig Config;
  Config.enableFolding(false);
  Config.enableConstantCSE(false);
  Config.setRegionSimplificationLevel(
      mlir::GreedySimplifyRegionLevel::Disabled);
  Config.setMaxIterations(64);
  Config.setMaxNumRewrites(100000);
  return Config;
}

// Create a pi at the rewriter's insertion point whose body is Body.
Value createArrow(mlir::PatternRewriter& Rewriter, Location Loc,
                  mlir::Type Type, Value Domain, Value Body) {
  auto Pi = schlap::PiOp::create(Rewriter, Loc, Type, Domain);
  mlir::OpBuilder::InsertionGuard Guard(Rewriter);
  Rewriter.createBlock(&Pi.getBody(), {}, {Type}, {Loc});
  schlap::YieldOp::create(Rewriter, Loc, Body);
  return Pi;
}

// Rewrite ∀ to Π.
struct ForallToPi : mlir::OpRewritePattern<schlap::ForallOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::ForallOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    auto Pi = schlap::PiOp::create(Rewriter, Op.getLoc(), Op.getType(),
                                   Op.getDomain());
    Rewriter.inlineRegionBefore(Op.getBody(), Pi.getBody(),
                                Pi.getBody().end());
    Rewriter.replaceOp(Op, Pi);
    return success();
  }
};

// Rewrite A ⇒ B to Π with a body that ignores its variable.
struct ImpliesToPi : mlir::OpRewritePattern<schlap::ImpliesOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::ImpliesOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    Rewriter.replaceOp(Op, createArrow(Rewriter, Op.getLoc(), Op.getType(),
                                       Op.getLhs(), Op.getRhs()));
    return success();
  }
};

// Rewrite ¬A to A ⇒ ⊥.
struct NotToPi : mlir::OpRewritePattern<schlap::NotOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::NotOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    Value False = schlap::FalseOp::create(Rewriter, Op.getLoc(), Op.getType());
    Rewriter.replaceOp(Op, createArrow(Rewriter, Op.getLoc(), Op.getType(),
                                       Op.getArg(), False));
    return success();
  }
};

// Rewrite A ⇔ B to (A ⇒ B) ∧ (B ⇒ A).
struct IffToAnd : mlir::OpRewritePattern<schlap::IffOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::IffOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    Location Loc = Op.getLoc();
    mlir::Type Type = Op.getType();
    Value To = createArrow(Rewriter, Loc, Type, Op.getLhs(), Op.getRhs());
    Value From = createArrow(Rewriter, Loc, Type, Op.getRhs(), Op.getLhs());
    Rewriter.replaceOpWithNewOp<schlap::AndOp>(Op, Type, To, From);
    return success();
  }
};

// Rewrite a ≠ b to a = b ⇒ ⊥.
struct NeToPi : mlir::OpRewritePattern<schlap::NeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::NeOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    Location Loc = Op.getLoc();
    mlir::Type Type = Op.getType();
    Value Eq = schlap::EqOp::create(Rewriter, Loc, Type, Op.getLhs(),
                                    Op.getRhs());
    Value False = schlap::FalseOp::create(Rewriter, Loc, Type);
    Rewriter.replaceOp(Op, createArrow(Rewriter, Loc, Type, Eq, False));
    return success();
  }
};

// Do not read an integer larger than this as a natural number.
constexpr int64_t MaxNumeral = 4096;

// Rewrite an integer n from 0 to MaxNumeral to succⁿ zero.
struct IntToNumeral : mlir::OpRewritePattern<schlap::IntOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::IntOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    int64_t N = Op.getValueAttr().getInt();
    if (N < 0 || N > MaxNumeral)
      return failure();
    Location Loc = Op.getLoc();
    mlir::Type Type = Op.getType();
    Value Numeral = schlap::ZeroOp::create(Rewriter, Loc, Type);
    for (int64_t I = 0; I != N; ++I)
      Numeral = schlap::SuccOp::create(Rewriter, Loc, Type, Numeral);
    Rewriter.replaceOp(Op, Numeral);
    return success();
  }
};

// Inline the body of a λ where it is applied (β).
struct Beta : mlir::OpRewritePattern<schlap::AppOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::AppOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    auto Lambda = Op.getLhs().getDefiningOp<schlap::LambdaOp>();
    if (!Lambda)
      return failure();
    mlir::Block& Body = Lambda.getBody().front();
    mlir::IRMapping Mapping;
    Mapping.map(Body.getArgument(0), Op.getRhs());
    for (Operation& Inner : Body.without_terminator())
      Rewriter.clone(Inner, Mapping);
    Rewriter.replaceOp(Op, Mapping.lookupOrDefault(yielded(Body)));
    return success();
  }
};

// Unfold a reference to a checked definition (δ).
struct Delta : mlir::OpRewritePattern<schlap::RefOp> {
  const llvm::StringMap<schlap::DefinitionOp>& Definitions;

  Delta(mlir::MLIRContext* Context,
        const llvm::StringMap<schlap::DefinitionOp>& Definitions)
      : OpRewritePattern(Context), Definitions(Definitions) {}

  LogicalResult matchAndRewrite(schlap::RefOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    auto It = Definitions.find(Op.getName());
    if (It == Definitions.end())
      return failure();
    schlap::DefinitionOp Definition = It->second;
    mlir::Block& Body = Definition.getBody().front();
    mlir::IRMapping Mapping;
    for (Operation& Inner : Body.without_terminator())
      Rewriter.clone(Inner, Mapping);
    Rewriter.replaceOp(Op, Mapping.lookupOrDefault(yielded(Body)));
    return success();
  }
};

// Reduce ℕ-rec on zero and on a successor (ι).
struct Iota : mlir::OpRewritePattern<schlap::NatRecOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schlap::NatRecOp Op,
                                mlir::PatternRewriter& Rewriter) const override {
    Operation* N = Op.getN().getDefiningOp();
    if (isa_and_nonnull<schlap::ZeroOp>(N)) {
      Rewriter.replaceOp(Op, Op.getBase());
      return success();
    }
    auto Succ = dyn_cast_or_null<schlap::SuccOp>(N);
    if (!Succ)
      return failure();
    Location Loc = Op.getLoc();
    mlir::Type Type = Op.getType();
    Value Pred = Succ.getArg();
    Value Rec = schlap::NatRecOp::create(Rewriter, Loc, Type, Op.getMotive(),
                                         Op.getBase(), Op.getStep(), Pred);
    Value Step = schlap::AppOp::create(Rewriter, Loc, Type, Op.getStep(),
                                       Pred);
    Rewriter.replaceOpWithNewOp<schlap::AppOp>(Op, Type, Step, Rec);
    return success();
  }
};

void populateDesugarPatterns(mlir::RewritePatternSet& Patterns) {
  Patterns.add<ForallToPi, ImpliesToPi, NotToPi, IffToAnd, NeToPi,
               IntToNumeral>(Patterns.getContext());
}

// Check the declarations of a module.
class Kernel {
  mlir::ModuleOp Module;
  schlap::VarNameFn VarName;
  mlir::Type Unknown;
  mlir::OpBuilder Builder;
  mlir::FrozenRewritePatternSet DesugarPatterns;
  mlir::FrozenRewritePatternSet NormalizePatterns;

  // Map each name to its first declaration.
  llvm::StringMap<Operation*> Declared;
  // Map each accepted declaration to the axioms it depends on.
  llvm::DenseMap<Operation*, mlir::ArrayAttr> Accepted;
  llvm::StringMap<schlap::DefinitionOp> Definitions;

  // Hold the state of the declaration being checked.
  Operation* Current = nullptr;
  llvm::SmallVector<Operation*> Holds;
  llvm::DenseMap<Value, Value> VarTypes;
  llvm::DenseMap<Value, Value> NormalForms;
  llvm::DenseMap<Value, Value> Types;
  std::set<std::string> Axioms;
  bool Quiet = false;

public:
  Kernel(mlir::ModuleOp Module, schlap::VarNameFn VarName)
      : Module(Module), VarName(VarName),
        Unknown(schlap::UnknownType::get(Module.getContext())),
        Builder(Module.getContext()) {
    mlir::MLIRContext* Context = Module.getContext();
    mlir::RewritePatternSet Desugar(Context);
    populateDesugarPatterns(Desugar);
    DesugarPatterns = std::move(Desugar);
    mlir::RewritePatternSet Normalize(Context);
    populateDesugarPatterns(Normalize);
    Normalize.add<Beta, Iota>(Context);
    Normalize.add<Delta>(Context, Definitions);
    NormalizePatterns = std::move(Normalize);
  }

  // Check every declaration of the module.
  LogicalResult run() {
    bool Failed = false;
    for (Operation& Op : *Module.getBody()) {
      if (!isDeclaration(Op))
        continue;
      llvm::StringRef Name = mlir::SymbolTable::getSymbolName(&Op);
      if (!Declared.try_emplace(Name, &Op).second) {
        Op.emitError() << "redefinition of '" << Name << "'";
        Failed = true;
      } else if (succeeded(checkDeclaration(&Op))) {
        addDefinition(&Op);
      } else {
        Failed = true;
      }
    }
    return mlir::failure(Failed);
  }

  // Check Decl against the accepted declarations of the module, add a copy
  // of Decl to the module if Decl checks, and return the axioms Decl
  // depends on.
  FailureOr<mlir::ArrayAttr> run(Operation* Decl) {
    mlir::ArrayAttr NoAxioms = mlir::ArrayAttr::get(Decl->getContext(), {});
    for (Operation& Op : *Module.getBody()) {
      Declared.try_emplace(mlir::SymbolTable::getSymbolName(&Op), &Op);
      auto Axioms = Op.getAttrOfType<mlir::ArrayAttr>("axioms");
      Accepted[&Op] = Axioms ? Axioms : NoAxioms;
      addDefinition(&Op);
    }

    llvm::StringRef Name = mlir::SymbolTable::getSymbolName(Decl);
    if (!Declared.try_emplace(Name, Decl).second)
      return Decl->emitError() << "redefinition of '" << Name << "'";
    if (failed(checkDeclaration(Decl)))
      return failure();
    Module.getBody()->push_back(Decl->clone());
    return Accepted.lookup(Decl);
  }

private:
  // Return true if Op is a theorem, definition or axiom.
  static bool isDeclaration(Operation& Op) {
    return isa<schlap::TheoremOp, schlap::DefinitionOp, schlap::AxiomOp>(Op);
  }

  void addDefinition(Operation* Op) {
    if (auto Definition = dyn_cast<schlap::DefinitionOp>(Op))
      Definitions[Definition.getSymName()] = Definition;
  }

  LogicalResult fail(Location Loc, const llvm::Twine& Message) {
    if (!Quiet)
      mlir::emitError(Loc) << Message;
    return failure();
  }

  std::string show(Value V) {
    return Printer(VarName).str(V);
  }

  std::string show(Value A, Value B, std::string& BStr) {
    Printer P(VarName);
    std::string AStr = P.str(A);
    BStr = P.str(B);
    return AStr;
  }

  static Location locOf(Value V, Location Use) {
    if (Operation* Op = V.getDefiningOp())
      return Op->getLoc();
    return Use;
  }

  // Check one declaration, accept it and mark it checked with its axioms.
  LogicalResult checkDeclaration(Operation* Op) {
    Accepted.erase(Op);
    Op->removeAttr("checked");
    Op->removeAttr("axioms");
    if (failed(mlir::verify(Op)))
      return failure();
    if (failed(mlir::applyPatternsGreedily(Op, DesugarPatterns,
                                           kernelRewriteConfig())))
      return Op->emitError("failed to desugar the declaration");

    Current = Op;
    VarTypes.clear();
    NormalForms.clear();
    Types.clear();
    Axioms.clear();
    Builder.setInsertionPointToEnd(createHoldBlock(Op->getLoc()));
    auto Cleanup = llvm::scope_exit([&] { destroyHolds(); });

    LogicalResult Result =
        llvm::TypeSwitch<Operation*, LogicalResult>(Op)
            .Case([&](schlap::TheoremOp Theorem) {
              Value Statement = yielded(Theorem.getStatement().front());
              if (failed(checkStatement(Statement, Op->getLoc())))
                return failure();
              return check(yielded(Theorem.getProof().front()), Statement,
                           Op->getLoc());
            })
            .Case([&](schlap::AxiomOp Axiom) {
              Axioms.insert(Axiom.getSymName().str());
              return checkStatement(yielded(Axiom.getStatement().front()),
                                    Op->getLoc());
            })
            .Case([&](schlap::DefinitionOp Definition) {
              Value Type = yielded(Definition.getSignature().front());
              if (failed(sortOfTerm(Type, Op->getLoc())))
                return failure();
              return check(yielded(Definition.getBody().front()), Type,
                           Op->getLoc());
            });
    if (failed(Result))
      return failure();

    setSkeletonTypes();
    Op->setAttr("checked", mlir::UnitAttr::get(Op->getContext()));
    llvm::SmallVector<mlir::Attribute> Refs;
    if (!isa<schlap::AxiomOp>(Op))
      for (const std::string& Name : Axioms)
        Refs.push_back(mlir::FlatSymbolRefAttr::get(Op->getContext(), Name));
    mlir::ArrayAttr AxiomRefs = mlir::ArrayAttr::get(Op->getContext(), Refs);
    if (!isa<schlap::AxiomOp>(Op))
      Op->setAttr("axioms", AxiomRefs);
    Accepted[Op] = AxiomRefs;
    return success();
  }

  LogicalResult checkStatement(Value Statement, Location Loc) {
    FailureOr<Value> Sort = sortOfTerm(Statement, Loc);
    if (failed(Sort))
      return failure();
    if (!isa<schlap::PropSortOp>(Sort->getDefiningOp()))
      return fail(Loc, "the statement '" + show(Statement) +
                           "' is not a proposition");
    return success();
  }

  // Set the skeleton type of each value of the declaration that has a type.
  void setSkeletonTypes() {
    mlir::MLIRContext* Context = Current->getContext();
    llvm::SmallVector<std::pair<Value, mlir::Type>> Updates;
    Quiet = true;
    llvm::SmallVector<std::pair<Value, Value>> Typed(Types.begin(),
                                                     Types.end());
    for (auto [V, T] : Typed) {
      Operation* Owner = isa<mlir::BlockArgument>(V)
          ? cast<mlir::BlockArgument>(V).getOwner()->getParentOp()
          : V.getDefiningOp();
      if (!Current->isProperAncestor(Owner))
        continue;
      FailureOr<Value> NT = normalize(T, Current->getLoc());
      if (failed(NT))
        continue;
      Operation* Head = NT->getDefiningOp();
      mlir::Type Skeleton;
      if (isa_and_nonnull<schlap::PropSortOp>(Head))
        Skeleton = schlap::PropType::get(Context);
      else if (isa_and_nonnull<schlap::NatOp>(Head))
        Skeleton = schlap::NatType::get(Context);
      else if (FailureOr<Value> S = sortOfTerm(*NT, Current->getLoc());
               succeeded(S) && isa<schlap::PropSortOp>(S->getDefiningOp()))
        Skeleton = schlap::ProofType::get(Context);
      else
        Skeleton = schlap::TermType::get(Context);
      Updates.emplace_back(V, Skeleton);
    }
    Quiet = false;
    for (auto [V, T] : Updates)
      V.setType(T);
  }

  Operation* createHold(Location Loc) {
    mlir::OpBuilder HoldBuilder(Current ? Current->getContext()
                                        : Module.getContext());
    auto Hold = schlap::HoldOp::create(HoldBuilder, Loc);
    Hold.getBody().emplaceBlock();
    Holds.push_back(Hold);
    return Hold;
  }

  mlir::Block* createHoldBlock(Location Loc) {
    return &createHold(Loc)->getRegion(0).front();
  }

  void destroyHolds() {
    for (Operation* Hold : Holds)
      Hold->dropAllReferences();
    for (Operation* Hold : Holds)
      Hold->erase();
    Holds.clear();
  }

  // Create terms at the builder's insertion point.

  template <typename OpTy, typename... Args>
  Value make(Location Loc, Args... Operands) {
    return OpTy::create(Builder, Loc, Unknown, Operands...);
  }

  Value makePi(Location Loc, Value Domain,
               llvm::function_ref<Value(Value)> BodyFn) {
    auto Pi = schlap::PiOp::create(Builder, Loc, Unknown, Domain);
    mlir::OpBuilder::InsertionGuard Guard(Builder);
    mlir::Block* Block =
        Builder.createBlock(&Pi.getBody(), {}, {Unknown}, {Loc});
    Value Body = BodyFn(Block->getArgument(0));
    schlap::YieldOp::create(Builder, Loc, Body);
    return Pi;
  }

  Value makeArrow(Location Loc, Value Domain, Value Body) {
    return makePi(Loc, Domain, [&](Value) { return Body; });
  }

  // Return the body of a binder with its variable replaced by Arg.
  Value instantiate(Operation* Binder, Value Arg) {
    mlir::Block& Body = Binder->getRegion(0).front();
    mlir::IRMapping Mapping;
    Mapping.map(Body.getArgument(0), Arg);
    for (Operation& Inner : Body.without_terminator())
      Builder.clone(Inner, Mapping);
    return Mapping.lookupOrDefault(yielded(Body));
  }

  // Apply F to Arg, reducing at once when F is a λ.
  Value apply(Location Loc, Value F, Value Arg) {
    if (auto Lambda = F.getDefiningOp<schlap::LambdaOp>())
      return instantiate(Lambda, Arg);
    return make<schlap::AppOp>(Loc, F, Arg);
  }

  // Return T or its normal form as an OpTy, or null if neither is one.
  template <typename OpTy>
  FailureOr<OpTy> headAs(Value T, Location Use) {
    if (auto Result = T.getDefiningOp<OpTy>())
      return Result;
    FailureOr<Value> NT = normalize(T, Use);
    if (failed(NT))
      return failure();
    return NT->template getDefiningOp<OpTy>();
  }

  // Copy the closed term a region yields to the insertion point.
  Value import(mlir::Region& Region) {
    mlir::Block& Body = Region.front();
    mlir::IRMapping Mapping;
    for (Operation& Inner : Body.without_terminator())
      Builder.clone(Inner, Mapping);
    return Mapping.lookupOrDefault(yielded(Body));
  }

  // Copy the operations V depends on in order, leaving variables free.
  void copyClosure(Value V, mlir::IRMapping& Mapping,
                   mlir::OpBuilder& HoldBuilder) {
    if (isa<mlir::BlockArgument>(V) || Mapping.contains(V))
      return;
    Operation* Op = V.getDefiningOp();
    for (Value Operand : Op->getOperands())
      copyClosure(Operand, Mapping, HoldBuilder);
    Op->walk([&](Operation* Inner) {
      if (Inner == Op)
        return;
      for (Value Operand : Inner->getOperands()) {
        Operation* Def = Operand.getDefiningOp();
        if (Def && !Op->isProperAncestor(Def))
          copyClosure(Operand, Mapping, HoldBuilder);
      }
    });
    HoldBuilder.clone(*Op, Mapping);
  }

  // Return the normal form of a well-typed term.
  FailureOr<Value> normalize(Value V, Location Use) {
    if (isa<mlir::BlockArgument>(V))
      return V;
    if (auto It = NormalForms.find(V); It != NormalForms.end())
      return It->second;
    mlir::Block* Block = createHoldBlock(locOf(V, Use));
    mlir::OpBuilder HoldBuilder = mlir::OpBuilder::atBlockEnd(Block);
    mlir::IRMapping Mapping;
    copyClosure(V, Mapping, HoldBuilder);
    schlap::YieldOp::create(HoldBuilder, locOf(V, Use),
                            Mapping.lookupOrDefault(V));
    llvm::SmallVector<Operation*> Ops;
    Block->walk([&](Operation* Op) { Ops.push_back(Op); });
    mlir::GreedyRewriteConfig Config = kernelRewriteConfig();
    Config.setScope(Block->getParent());
    Config.setStrictness(mlir::GreedyRewriteStrictness::AnyOp);
    if (failed(mlir::applyOpPatternsGreedily(Ops, NormalizePatterns, Config)))
      return fail(locOf(V, Use), "'" + show(V) +
                                     "' does not normalize within the limit");
    Value Result = yielded(*Block);
    NormalForms[V] = Result;
    NormalForms[Result] = Result;
    return Result;
  }

  // Return true if two normal forms are equal up to bound variables.
  bool alphaEquivalent(Value A, Value B, llvm::DenseMap<Value, Value>& Bound) {
    if (isa<mlir::BlockArgument>(A) || isa<mlir::BlockArgument>(B)) {
      auto It = Bound.find(A);
      if (It != Bound.end())
        return It->second == B;
      return A == B;
    }
    if (A == B)
      return true;
    Operation* OpA = A.getDefiningOp();
    Operation* OpB = B.getDefiningOp();
    if (OpA->getName() != OpB->getName() ||
        OpA->getAttrDictionary() != OpB->getAttrDictionary() ||
        OpA->getNumOperands() != OpB->getNumOperands() ||
        OpA->getNumRegions() != OpB->getNumRegions())
      return false;
    for (auto [X, Y] : llvm::zip(OpA->getOperands(), OpB->getOperands()))
      if (!alphaEquivalent(X, Y, Bound))
        return false;
    for (auto [RA, RB] : llvm::zip(OpA->getRegions(), OpB->getRegions())) {
      mlir::Block& BA = RA.front();
      mlir::Block& BB = RB.front();
      if (BA.getNumArguments() != BB.getNumArguments())
        return false;
      for (auto [X, Y] : llvm::zip(BA.getArguments(), BB.getArguments()))
        Bound[X] = Y;
      bool Equal = alphaEquivalent(yielded(BA), yielded(BB), Bound);
      for (Value X : BA.getArguments())
        Bound.erase(X);
      if (!Equal)
        return false;
    }
    return true;
  }

  FailureOr<bool> convertible(Value A, Value B, Location Use) {
    llvm::DenseMap<Value, Value> Same;
    if (alphaEquivalent(A, B, Same))
      return true;
    FailureOr<Value> NA = normalize(A, Use);
    if (failed(NA))
      return failure();
    FailureOr<Value> NB = normalize(B, Use);
    if (failed(NB))
      return failure();
    llvm::DenseMap<Value, Value> Bound;
    return alphaEquivalent(*NA, *NB, Bound);
  }

  // Return the normal form of the type of a well-typed term and require
  // that it is a sort.
  FailureOr<Value> sortOfTerm(Value T, Location Use) {
    FailureOr<Value> S = infer(T, Use);
    if (failed(S))
      return failure();
    FailureOr<Value> NS = normalize(*S, Use);
    if (failed(NS))
      return failure();
    if (!isa_and_nonnull<schlap::PropSortOp, schlap::TypeSortOp>(
            NS->getDefiningOp()))
      return fail(locOf(T, Use), "expected a type but '" + show(T) +
                                     "' has type '" + show(*S) + "'");
    return *NS;
  }

  // Return the sort of a type that is known to be well formed.
  FailureOr<Value> sortOfType(Value T, Location Use) {
    FailureOr<Value> NT = normalize(T, Use);
    if (failed(NT))
      return failure();
    return sortOfTerm(*NT, Use);
  }

  LogicalResult checkProp(Value T, Location Use) {
    FailureOr<Value> S = sortOfTerm(T, Use);
    if (failed(S))
      return failure();
    if (!isa<schlap::PropSortOp>(S->getDefiningOp()))
      return fail(locOf(T, Use), "expected a proposition but '" + show(T) +
                                     "' is a type");
    return success();
  }

  // Require a well-formed type to be a proposition.
  LogicalResult requirePropType(Value T, llvm::StringRef Form, Location Loc) {
    FailureOr<Value> S = sortOfType(T, Loc);
    if (failed(S))
      return failure();
    if (!isa<schlap::PropSortOp>(S->getDefiningOp()))
      return fail(Loc, Form + " proves only propositions but '" + show(T) +
                           "' is expected");
    return success();
  }

  // Return the normal form of the type of V if it is an OpTy.
  template <typename OpTy>
  FailureOr<OpTy> inferAs(Value V, Location Use, llvm::StringRef What,
                          llvm::StringRef Form) {
    FailureOr<Value> T = infer(V, Use);
    if (failed(T))
      return failure();
    FailureOr<OpTy> Result = headAs<OpTy>(*T, Use);
    if (failed(Result) || *Result)
      return Result;
    return fail(locOf(V, Use), Form + " expects a proof of " + What +
                                   " but got a proof of '" + show(*T) + "'");
  }

  // Return the normal form of the expected type if it is an OpTy.
  template <typename OpTy>
  FailureOr<OpTy> expectAs(Value T, Location Loc, llvm::StringRef Form,
                           llvm::StringRef What) {
    FailureOr<OpTy> Result = headAs<OpTy>(T, Loc);
    if (failed(Result) || *Result)
      return Result;
    return fail(Loc, Form + " proves " + What + " but '" + show(T) +
                         "' is expected");
  }

  // Require a reference to name a declaration checked before this one.
  FailureOr<Operation*> lookup(schlap::RefOp Ref) {
    llvm::StringRef Name = Ref.getName();
    Operation* Declaration = Declared.lookup(Name);
    if (Declaration == Current)
      return fail(Ref.getLoc(), "'" + Name + "' refers to itself");
    if (!Declaration)
      return fail(Ref.getLoc(), "'" + Name +
                                    "' is not declared before this");
    if (!Accepted.contains(Declaration))
      return fail(Ref.getLoc(), "'" + Name + "' did not check");
    return Declaration;
  }

  FailureOr<Value> inferRef(schlap::RefOp Ref) {
    FailureOr<Operation*> Declaration = lookup(Ref);
    if (failed(Declaration))
      return failure();
    return llvm::TypeSwitch<Operation*, Value>(*Declaration)
        .Case([&](schlap::AxiomOp Axiom) {
          Axioms.insert(Axiom.getSymName().str());
          return import(Axiom.getStatement());
        })
        .Case<schlap::TheoremOp, schlap::DefinitionOp>([&](auto Op) {
          mlir::ArrayAttr Names = Accepted.lookup(Op);
          for (auto Name : Names.template getAsRange<mlir::FlatSymbolRefAttr>())
            Axioms.insert(Name.getValue().str());
          if constexpr (std::is_same_v<decltype(Op), schlap::TheoremOp>)
            return import(Op.getStatement());
          else
            return import(Op.getSignature());
        })
        .Default([](Operation*) { return Value(); });
  }

  // Return the sort of the body of an ℕ-rec motive.
  FailureOr<Value> checkMotive(Value Motive, Location Use) {
    Location Loc = locOf(Motive, Use);
    if (auto Lambda = Motive.getDefiningOp<schlap::LambdaOp>()) {
      Value Var = Lambda.getBody().front().getArgument(0);
      Value Nat = make<schlap::NatOp>(Loc);
      VarTypes[Var] = Nat;
      Types[Var] = Nat;
      FailureOr<Value> Sort = sortOfTerm(yielded(Lambda), Loc);
      if (failed(Sort))
        return failure();
      Types[Motive] = makeArrow(Loc, Nat, *Sort);
      return Sort;
    }
    FailureOr<Value> T = infer(Motive, Use);
    if (failed(T))
      return failure();
    FailureOr<Value> NT = normalize(*T, Use);
    if (failed(NT))
      return failure();
    auto Pi = NT->getDefiningOp<schlap::PiOp>();
    if (Pi && isa_and_nonnull<schlap::NatOp>(Pi.getDomain().getDefiningOp())) {
      Operation* Body = yielded(Pi).getDefiningOp();
      if (isa_and_nonnull<schlap::PropSortOp, schlap::TypeSortOp>(Body))
        return Value(Body->getResult(0));
    }
    return fail(Loc, "ℕ-rec expects a motive from ℕ to a sort but got '" +
                         show(*T) + "'");
  }

  LogicalResult record(Value V, Value T) {
    Types[V] = T;
    return success();
  }

  // Check that V has type T.
  LogicalResult check(Value V, Value T, Location Use) {
    Location Loc = locOf(V, Use);
    Operation* Op = V.getDefiningOp();
    if (!Op)
      return checkByInference(V, T, Use);
    return llvm::TypeSwitch<Operation*, LogicalResult>(Op)
        .Case([&](schlap::LambdaOp Lambda) -> LogicalResult {
          FailureOr<schlap::PiOp> Pi = headAs<schlap::PiOp>(T, Loc);
          if (failed(Pi))
            return failure();
          if (!*Pi)
            return fail(Loc, "λ binds a variable but '" + show(T) +
                                 "' is expected, which is not a ∀ or ⇒");
          Value Var = Lambda.getBody().front().getArgument(0);
          VarTypes[Var] = Pi->getDomain();
          Types[Var] = Pi->getDomain();
          Value Body = instantiate(*Pi, Var);
          if (failed(check(yielded(Lambda), Body, Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::AndIntroOp Intro) -> LogicalResult {
          FailureOr<schlap::AndOp> And = headAs<schlap::AndOp>(T, Loc);
          if (failed(And))
            return failure();
          if (!*And)
            return checkByInference(V, T, Use);
          if (failed(check(Intro.getLhs(), And->getLhs(), Loc)) ||
              failed(check(Intro.getRhs(), And->getRhs(), Loc)))
            return failure();
          return record(V, T);
        })
        .Case<schlap::OrInlOp, schlap::OrInrOp>([&](auto Inj) -> LogicalResult {
          bool Left = std::is_same_v<decltype(Inj), schlap::OrInlOp>;
          FailureOr<schlap::OrOp> Or = expectAs<schlap::OrOp>(
              T, Loc, Left ? "∨-inl" : "∨-inr", "a disjunction");
          if (failed(Or))
            return failure();
          Value Side = Left ? Or->getLhs() : Or->getRhs();
          if (failed(check(Inj.getArg(), Side, Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::OrElimOp Elim) -> LogicalResult {
          if (failed(requirePropType(T, "∨-elim", Loc)))
            return failure();
          FailureOr<schlap::OrOp> Or = inferAs<schlap::OrOp>(
              Elim.getFirst(), Loc, "a disjunction", "∨-elim");
          if (failed(Or))
            return failure();
          if (failed(check(Elim.getSecond(),
                           makeArrow(Loc, Or->getLhs(), T), Loc)) ||
              failed(check(Elim.getThird(),
                           makeArrow(Loc, Or->getRhs(), T), Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::FalseElimOp Elim) -> LogicalResult {
          if (failed(check(Elim.getArg(), make<schlap::FalseOp>(Loc), Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::ExistsIntroOp Intro) -> LogicalResult {
          FailureOr<schlap::ExistsOp> Exists = expectAs<schlap::ExistsOp>(
              T, Loc, "∃-intro", "an existential");
          if (failed(Exists))
            return failure();
          if (failed(check(Intro.getLhs(), Exists->getDomain(), Loc)) ||
              failed(check(Intro.getRhs(),
                           instantiate(*Exists, Intro.getLhs()), Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::ExistsElimOp Elim) -> LogicalResult {
          if (failed(requirePropType(T, "∃-elim", Loc)))
            return failure();
          FailureOr<schlap::ExistsOp> Exists = inferAs<schlap::ExistsOp>(
              Elim.getLhs(), Loc, "an existential", "∃-elim");
          if (failed(Exists))
            return failure();
          Value Fn = makePi(Loc, Exists->getDomain(), [&](Value Var) {
            return makeArrow(Loc, instantiate(*Exists, Var), T);
          });
          if (failed(check(Elim.getRhs(), Fn, Loc)))
            return failure();
          return record(V, T);
        })
        .Case([&](schlap::ReflOp) -> LogicalResult {
          FailureOr<schlap::EqOp> Eq =
              expectAs<schlap::EqOp>(T, Loc, "refl", "an equality");
          if (failed(Eq))
            return failure();
          FailureOr<bool> Same =
              convertible(Eq->getLhs(), Eq->getRhs(), Loc);
          if (failed(Same))
            return failure();
          if (!*Same) {
            FailureOr<Value> NL = normalize(Eq->getLhs(), Loc);
            FailureOr<Value> NR = normalize(Eq->getRhs(), Loc);
            if (failed(NL) || failed(NR))
              return failure();
            std::string Rhs;
            std::string Lhs = show(*NL, *NR, Rhs);
            return fail(Loc, "refl cannot prove '" + show(T) +
                                 "' because its sides compute to '" + Lhs +
                                 "' and '" + Rhs + "'");
          }
          return record(V, T);
        })
        .Default([&](Operation*) { return checkByInference(V, T, Use); });
  }

  LogicalResult checkByInference(Value V, Value T, Location Use) {
    FailureOr<Value> U = infer(V, Use);
    if (failed(U))
      return failure();
    FailureOr<bool> Same = convertible(*U, T, Use);
    if (failed(Same))
      return failure();
    if (!*Same) {
      std::string Actual;
      std::string Expected = show(T, *U, Actual);
      return fail(locOf(V, Use),
                  "expected '" + Expected + "' but got '" + Actual + "'");
    }
    return record(V, T);
  }

  // Infer the type of V.
  FailureOr<Value> infer(Value V, Location Use) {
    FailureOr<Value> T = inferUnrecorded(V, Use);
    if (succeeded(T))
      Types[V] = *T;
    return T;
  }

  FailureOr<Value> inferUnrecorded(Value V, Location Use) {
    Location Loc = locOf(V, Use);
    Operation* Op = V.getDefiningOp();
    if (!Op) {
      auto It = VarTypes.find(V);
      if (It == VarTypes.end())
        return fail(Use, "unbound variable '" + show(V) + "'");
      return It->second;
    }
    auto PropSort = [&] { return make<schlap::PropSortOp>(Loc); };
    auto CannotInfer = [&](llvm::StringRef Form) {
      return fail(Loc, "cannot infer what " + Form +
                           " proves here; use it where its proposition "
                           "is expected");
    };
    return llvm::TypeSwitch<Operation*, FailureOr<Value>>(Op)
        .Case([&](schlap::PropSortOp) {
          return make<schlap::TypeSortOp>(Loc);
        })
        .Case([&](schlap::TypeSortOp) {
          return fail(Loc, "Type has no type");
        })
        .Case([&](schlap::NatOp) { return make<schlap::TypeSortOp>(Loc); })
        .Case([&](schlap::ZeroOp) { return make<schlap::NatOp>(Loc); })
        .Case([&](schlap::SuccOp Succ) -> FailureOr<Value> {
          Value Nat = make<schlap::NatOp>(Loc);
          if (failed(check(Succ.getArg(), Nat, Loc)))
            return failure();
          return Nat;
        })
        .Case<schlap::TrueOp, schlap::FalseOp>([&](Operation*) {
          return PropSort();
        })
        .Case<schlap::AndOp, schlap::OrOp>([&](Operation*) -> FailureOr<Value> {
          if (failed(checkProp(Op->getOperand(0), Loc)) ||
              failed(checkProp(Op->getOperand(1), Loc)))
            return failure();
          return PropSort();
        })
        .Case([&](schlap::EqOp Eq) -> FailureOr<Value> {
          FailureOr<Value> A = infer(Eq.getLhs(), Loc);
          if (failed(A) || failed(sortOfType(*A, Loc)) ||
              failed(check(Eq.getRhs(), *A, Loc)))
            return failure();
          return PropSort();
        })
        .Case([&](schlap::PiOp Pi) -> FailureOr<Value> {
          if (failed(sortOfTerm(Pi.getDomain(), Loc)))
            return failure();
          Value Var = Pi.getBody().front().getArgument(0);
          VarTypes[Var] = Pi.getDomain();
          Types[Var] = Pi.getDomain();
          FailureOr<Value> Sort = sortOfTerm(yielded(Pi), Loc);
          if (failed(Sort))
            return failure();
          if (isa<schlap::PropSortOp>(Sort->getDefiningOp()))
            return PropSort();
          return make<schlap::TypeSortOp>(Loc);
        })
        .Case([&](schlap::ExistsOp Exists) -> FailureOr<Value> {
          if (failed(sortOfTerm(Exists.getDomain(), Loc)))
            return failure();
          Value Var = Exists.getBody().front().getArgument(0);
          VarTypes[Var] = Exists.getDomain();
          Types[Var] = Exists.getDomain();
          if (failed(checkProp(yielded(Exists), Loc)))
            return failure();
          return PropSort();
        })
        .Case([&](schlap::LambdaOp) {
          return fail(Loc, "cannot infer the type of λ here; use it where "
                           "a ∀ or ⇒ is expected");
        })
        .Case([&](schlap::AppOp App) -> FailureOr<Value> {
          FailureOr<Value> F = infer(App.getLhs(), Loc);
          if (failed(F))
            return failure();
          FailureOr<schlap::PiOp> Pi = headAs<schlap::PiOp>(*F, Loc);
          if (failed(Pi))
            return failure();
          if (!*Pi)
            return fail(Loc, "cannot apply '" + show(App.getLhs()) +
                                 "' of type '" + show(*F) +
                                 "', which is not a ∀ or ⇒");
          if (failed(check(App.getRhs(), Pi->getDomain(), Loc)))
            return failure();
          return instantiate(*Pi, App.getRhs());
        })
        .Case([&](schlap::RefOp Ref) { return inferRef(Ref); })
        .Case([&](schlap::TopIntroOp) { return make<schlap::TrueOp>(Loc); })
        .Case([&](schlap::AndIntroOp Intro) -> FailureOr<Value> {
          FailureOr<Value> A = infer(Intro.getLhs(), Loc);
          if (failed(A) || failed(requirePropType(*A, "∧-intro", Loc)))
            return failure();
          FailureOr<Value> B = infer(Intro.getRhs(), Loc);
          if (failed(B) || failed(requirePropType(*B, "∧-intro", Loc)))
            return failure();
          return make<schlap::AndOp>(Loc, *A, *B);
        })
        .Case<schlap::AndLeftOp, schlap::AndRightOp>(
            [&](auto Proj) -> FailureOr<Value> {
              bool Left = std::is_same_v<decltype(Proj), schlap::AndLeftOp>;
              FailureOr<schlap::AndOp> And = inferAs<schlap::AndOp>(
                  Proj.getArg(), Loc, "a conjunction",
                  Left ? "∧-left" : "∧-right");
              if (failed(And))
                return failure();
              return Left ? And->getLhs() : And->getRhs();
            })
        .Case([&](schlap::SubstOp Subst) -> FailureOr<Value> {
          FailureOr<schlap::EqOp> Eq = inferAs<schlap::EqOp>(
              Subst.getFirst(), Loc, "an equality", "subst");
          if (failed(Eq))
            return failure();
          FailureOr<Value> A = infer(Eq->getLhs(), Loc);
          if (failed(A))
            return failure();
          Value Motive = Subst.getSecond();
          if (failed(check(Motive, makeArrow(Loc, *A, PropSort()), Loc)) ||
              failed(check(Subst.getThird(),
                           apply(Loc, Motive, Eq->getLhs()), Loc)))
            return failure();
          return apply(Loc, Motive, Eq->getRhs());
        })
        .Case([&](schlap::NatRecOp Rec) -> FailureOr<Value> {
          Value Motive = Rec.getMotive();
          if (failed(checkMotive(Motive, Loc)))
            return failure();
          Value Nat = make<schlap::NatOp>(Loc);
          Value Zero = make<schlap::ZeroOp>(Loc);
          Value Step = makePi(Loc, Nat, [&](Value K) {
            Value MK = apply(Loc, Motive, K);
            Value SK = make<schlap::SuccOp>(Loc, K);
            return makeArrow(Loc, MK, apply(Loc, Motive, SK));
          });
          if (failed(check(Rec.getBase(), apply(Loc, Motive, Zero), Loc)) ||
              failed(check(Rec.getStep(), Step, Loc)) ||
              failed(check(Rec.getN(), Nat, Loc)))
            return failure();
          return apply(Loc, Motive, Rec.getN());
        })
        .Case([&](schlap::OrInlOp) { return CannotInfer("∨-inl"); })
        .Case([&](schlap::OrInrOp) { return CannotInfer("∨-inr"); })
        .Case([&](schlap::OrElimOp) { return CannotInfer("∨-elim"); })
        .Case([&](schlap::FalseElimOp) { return CannotInfer("⊥-elim"); })
        .Case([&](schlap::ExistsIntroOp) { return CannotInfer("∃-intro"); })
        .Case([&](schlap::ExistsElimOp) { return CannotInfer("∃-elim"); })
        .Case([&](schlap::ReflOp) { return CannotInfer("refl"); })
        .Case([&](schlap::IntOp Int) {
          return fail(Loc, "the integer " +
                               llvm::Twine(Int.getValueAttr().getInt()) +
                               " is not a natural number from 0 to " +
                               llvm::Twine(MaxNumeral));
        })
        .Default([&](Operation* Op) {
          return fail(Loc, "'" + Op->getName().getStringRef() +
                               "' is not a term of the proof kernel");
        });
  }
};

// Return the name of a NameLoc or "".
std::string nameOfLoc(Location Loc) {
  if (auto Name = dyn_cast<mlir::NameLoc>(Loc))
    return Name.getName().str();
  return "";
}

// Check proof terms against their propositions.
class CheckProofsPass
    : public schlap::impl::CheckProofsPassBase<CheckProofsPass> {
  using Base = schlap::impl::CheckProofsPassBase<CheckProofsPass>;

public:
  using Base::Base;

  void runOnOperation() override {
    auto Module = dyn_cast<mlir::ModuleOp>(getOperation());
    if (!Module) {
      getOperation()->emitError("expects a module");
      return signalPassFailure();
    }
    if (failed(schlap::checkProofs(Module, nameOfLoc)))
      signalPassFailure();
  }
};
}  // namespace

LogicalResult schlap::checkProofs(mlir::ModuleOp Module, VarNameFn VarName) {
  return Kernel(Module, VarName).run();
}

schlap::KernelState::KernelState(mlir::MLIRContext* Context)
    : Module(mlir::ModuleOp::create(mlir::UnknownLoc::get(Context))) {}

FailureOr<mlir::ArrayAttr> schlap::checkDeclaration(Operation* Decl,
                                                   KernelState& State,
                                                   VarNameFn VarName) {
  return Kernel(*State.Module, VarName).run(Decl);
}
