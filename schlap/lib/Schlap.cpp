#include <schlap/ApplyRule.h>
#include <schlap/Check.h>
#include <schlap/Dialect.h>
#include <schlap/Kernel.h>
#include <schlap/Passes.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Pass/PassManager.h>
#include <schir/Context.h>
#include <schir/MlirHelper.h>
#include <schir/Value.h>
#include <llvm/ADT/ScopeExit.h>
#include <llvm/ADT/StringExtras.h>

namespace {
// Double each opening brace of a message.
std::string escapeBraces(llvm::StringRef Message) {
  std::string Result;
  for (char Ch : Message) {
    Result += Ch;
    if (Ch == '{')
      Result += Ch;
  }
  return Result;
}

// Reemit each diagnostic with its opening braces doubled.
class EscapeDiagnostics {
public:
  explicit EscapeDiagnostics(mlir::MLIRContext* Context)
    : Context(Context),
      Handler(Context, [this](mlir::Diagnostic& D) { return escape(D); }) {}

private:
  mlir::MLIRContext* Context;
  bool Forwarding = false;
  mlir::ScopedDiagnosticHandler Handler;

  llvm::LogicalResult escape(mlir::Diagnostic& D) {
    if (Forwarding)
      return mlir::failure();
    mlir::Diagnostic Escaped(D.getLocation(), D.getSeverity());
    Escaped << escapeBraces(D.str());
    for (mlir::Diagnostic& Note : D.getNotes())
      Escaped.attachNote(Note.getLocation()) << escapeBraces(Note.str());
    Forwarding = true;
    Context->getDiagEngine().emit(std::move(Escaped));
    Forwarding = false;
    return mlir::success();
  }
};

// Hold the declarations the kernel accepted in a context.
schir::ContextLocal KernelStateLocal;

// Return the declarations the kernel accepted in C.
schlap::KernelState& getKernelState(schir::Context& C) {
  if (auto* State =
          schir::any_cast<schlap::KernelState>(KernelStateLocal.get(C)))
    return *State;
  auto New = std::make_shared<schlap::KernelState>(C.MLIRContext.get());
  schlap::KernelState& State = *New;
  KernelStateLocal.set(C, C.CreateSharedAny(std::move(New)));
  return State;
}
}  // namespace

extern "C" {
schir::ContextLocal schlap_current_module;

// Register the schlap dialect, the dialects it is used with, and its passes.
void schlap_init(schir::Context& C, schir::ValueRefs) {
  C.DialectRegistry->insert<schlap::SchlapDialect>();
  C.DialectRegistry->insert<mlir::arith::ArithDialect>();
  C.DialectRegistry->insert<mlir::func::FuncDialect>();

  schlap::registerSchlapPasses();
  C.Cont();
}

// Infer, normalize and check a specification and return its number of
// distinct states, raising an error with the trace if a check fails.
// (schlap-check spec loc state-limit check-deadlock)
void schlap_check(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 4)
    return C.RaiseError("invalid arity");
  auto Spec = dyn_cast_or_null<schlap::SpecOp>(
      dyn_cast<mlir::Operation>(Args[0]));
  if (!Spec)
    return C.RaiseError("expecting schlap.spec: {}", Args[0]);
  if (!isa<schir::Int>(Args[2]) || cast<schir::Int>(Args[2]) < 0)
    return C.RaiseError("expecting a state limit: {}", Args[2]);

  schlap::CheckOptions Options;
  Options.StateLimit = static_cast<uint64_t>(cast<schir::Int>(Args[2]));
  Options.CheckDeadlock = !(isa<schir::Bool>(Args[3]) &&
                            !cast<schir::Bool>(Args[3]));

  schir::Value Name = C.CreateSymbol(Spec.getSymName());
  uint64_t NumStates = 0;
  llvm::LogicalResult Result = schir::mlir_helper::WithDiagnosticsHandler(
    C, Args[1].getSourceLocation(),
    [&]() -> llvm::LogicalResult {
      EscapeDiagnostics Escape(Spec->getContext());
      mlir::PassManager PM(Spec->getContext());
      PM.addPass(schlap::createInferTypesPass());
      PM.addPass(schlap::createNormalizeActionsPass());
      if (mlir::failed(PM.run(Spec)))
        return mlir::failure();
      llvm::FailureOr<schlap::CheckStats> Stats =
          schlap::checkSpec(Spec, Options);
      if (mlir::failed(Stats))
        return mlir::failure();
      NumStates = Stats->NumStates;
      return mlir::success();
    },
    "check failed for spec: {}", Name);
  if (mlir::failed(Result))
    return;
  C.Cont(schir::Int(static_cast<int32_t>(NumStates)));
}

// Apply a rule to a function of another module, check the specification
// and return its number of distinct states, raising an error with the
// trace if a check fails. Erase the specification in either case.
// (schlap-check-rule rule function loc state-limit)
void schlap_check_rule(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 4)
    return C.RaiseError("invalid arity");
  auto Rule = dyn_cast_or_null<schlap::RuleOp>(
      dyn_cast<mlir::Operation>(Args[0]));
  if (!Rule)
    return C.RaiseError("expecting schlap.rule: {}", Args[0]);
  auto* Fn = dyn_cast<mlir::Operation>(Args[1]);
  if (!Fn || !schlap::getRuleBody(Fn))
    return C.RaiseError("expecting a function with a body: {}", Args[1]);
  if (Fn->getContext() != Rule->getContext())
    return C.RaiseError("expecting a function in the context of the rule: {}",
                        Args[1]);
  auto Module = dyn_cast_or_null<mlir::ModuleOp>(Rule->getParentOp());
  if (!Module)
    return C.RaiseError("expecting a rule in a module: {}", Args[0]);
  if (!isa<schir::Int>(Args[3]) || cast<schir::Int>(Args[3]) < 0)
    return C.RaiseError("expecting a state limit: {}", Args[3]);

  schlap::CheckOptions Options;
  Options.StateLimit = static_cast<uint64_t>(cast<schir::Int>(Args[3]));

  std::string Message = "rule " + escapeBraces(Rule.getSymName()) +
                        " does not hold for function: {}";
  schir::Value Name = C.CreateSymbol(mlir::SymbolTable::getSymbolName(Fn));
  uint64_t NumStates = 0;
  llvm::LogicalResult Result = schir::mlir_helper::WithDiagnosticsHandler(
    C, Args[2].getSourceLocation(),
    [&]() -> llvm::LogicalResult {
      EscapeDiagnostics Escape(Rule->getContext());
      mlir::OpBuilder Builder = mlir::OpBuilder::atBlockEnd(Module.getBody());
      llvm::FailureOr<schlap::SpecOp> Spec =
          schlap::applyRule(Rule, Fn, Builder);
      if (mlir::failed(Spec))
        return mlir::failure();
      llvm::scope_exit EraseSpec([&] { Spec->erase(); });
      mlir::PassManager PM(Rule->getContext());
      PM.addPass(schlap::createInferTypesPass());
      PM.addPass(schlap::createNormalizeActionsPass());
      if (mlir::failed(PM.run(*Spec)))
        return mlir::failure();
      llvm::FailureOr<schlap::CheckStats> Stats =
          schlap::checkSpec(*Spec, Options);
      if (mlir::failed(Stats))
        return mlir::failure();
      NumStates = Stats->NumStates;
      return mlir::success();
    },
    Message, Name);
  if (mlir::failed(Result))
    return;
  C.Cont(schir::Int(static_cast<int32_t>(NumStates)));
}

// Erase a theorem, definition or axiom that failed to build.
void schlap_discard_declaration(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op || !isa<schlap::TheoremOp, schlap::DefinitionOp,
                  schlap::AxiomOp>(Op))
    return C.RaiseError("expecting a schlap declaration: {}", Args[0]);
  Op->erase();
  C.Cont();
}

// Check a theorem, definition or axiom with the kernel and return the
// names of the axioms it depends on, or erase it and raise the errors.
void schlap_check_declaration(schir::Context& C, schir::ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  mlir::Operation* Op = dyn_cast<mlir::Operation>(Args[0]);
  if (!Op || !isa<schlap::TheoremOp, schlap::DefinitionOp,
                  schlap::AxiomOp>(Op))
    return C.RaiseError("expecting a schlap declaration: {}", Args[0]);
  auto SourceLoc = [](mlir::Location Loc) {
    return schir::SourceLocation(mlir::OpaqueLoc
      ::getUnderlyingLocationOrNull<schir::SourceLocationEncoding*>(Loc));
  };
  // Return the identifier at the source location of a variable.
  auto VarName = [&](mlir::Location Loc) -> std::string {
    schir::FullSourceLocation Full =
        C.getFullSourceLocation(SourceLoc(Loc));
    if (!Full.isValid())
      return "";
    schir::SourceLineContext Line = Full.getLineContext();
    if (Line.Column >= Line.LineRange.size())
      return "";
    llvm::StringRef Rest = Line.LineRange.drop_front(Line.Column);
    return Rest.take_until([](char Ch) {
      return llvm::isSpace(Ch) || llvm::StringRef("()[]{}\"';").contains(Ch);
    }).str();
  };

  schlap::KernelState& State = getKernelState(C);
  llvm::StringRef Name = mlir::SymbolTable::getSymbolName(Op);
  schir::Value NameSym = C.CreateSymbol(Name);
  mlir::ArrayAttr AxiomRefs;
  llvm::LogicalResult Result = schir::mlir_helper::WithDiagnosticsHandler(
      C, SourceLoc(Op->getLoc()),
      [&] {
        llvm::FailureOr<mlir::ArrayAttr> Refs =
            schlap::checkDeclaration(Op, State, VarName);
        if (mlir::failed(Refs))
          return mlir::failure();
        AxiomRefs = *Refs;
        return mlir::success();
      },
      "declaration does not check: {}", NameSym);
  if (mlir::failed(Result)) {
    Op->erase();
    return;
  }

  llvm::SmallVector<schir::Value> Axioms;
  for (auto Ref : AxiomRefs.getAsRange<mlir::FlatSymbolRefAttr>())
    Axioms.push_back(C.CreateSymbol(Ref.getValue()));
  C.Cont(C.CreateList(Axioms));
}
}  // extern "C"
