//===- Builtins.cpp - Builtin functions for SchirScheme ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines builtins and builtin syntax for SchirScheme
//
//===----------------------------------------------------------------------===//

#include "TemplateGen.h"
#include "schir/Builtins.h"
#include "schir/Context.h"
#include "schir/Dialect.h"
#include "schir/OpGen.h"
#include "schir/SourceManager.h"
#include "schir/Value.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DynamicLibrary.h"
#include "cassert"
#include "memory"

namespace mlir {
class Value;
}

namespace schir::builtins_var {
schir::ExternSyntax<>      define_library;
schir::ExternSyntax<>      begin;
schir::ExternSyntax<>      export_;
schir::ExternSyntax<>      include;
schir::ExternSyntax<>      include_ci;
schir::ExternSyntax<>      include_library_declarations;
schir::ContextLocal        parse_source_file;

schir::ExternBuiltinSyntax cond_expand;
schir::ExternBuiltinSyntax define;
schir::ExternBuiltinSyntax define_binding;
schir::ExternBuiltinSyntax define_syntax;
schir::ExternBuiltinSyntax syntax_rules;
schir::ExternBuiltinSyntax syntax_fn;
schir::ExternBuiltinSyntax if_;
schir::ExternBuiltinSyntax lambda;
schir::ExternBuiltinSyntax quasiquote;
schir::ExternBuiltinSyntax quote;
schir::ExternBuiltinSyntax set;
schir::ExternBuiltinSyntax syntax_error;
schir::ExternBuiltinSyntax case_lambda;

schir::ExternFunction apply;
schir::ExternFunction add;
schir::ExternFunction sub;
schir::ExternFunction div;
schir::ExternFunction mul;
schir::ExternFunction is_positive;
schir::ExternFunction is_zero;
schir::ExternFunction list;
schir::ExternFunction length;
schir::ExternFunction cons;
schir::ExternFunction source_cons;
schir::ExternFunction car;
schir::ExternFunction cdr;
schir::ExternFunction append;
schir::ExternFunction vector;
schir::ExternFunction make_vector;
schir::ExternFunction vector_length;
schir::ExternFunction vector_ref;
schir::ExternFunction vector_set;
schir::ExternFunction make_list;
schir::ExternFunction list_set;
schir::ExternFunction list_ref;
schir::ExternFunction dump;
schir::ExternFunction write;
schir::ExternFunction newline;
schir::ExternFunction string_append;
schir::ExternFunction string_copy;
schir::ExternFunction string_length;
schir::ExternFunction string_ref;
schir::ExternFunction number_to_string;
schir::ExternFunction eq;
schir::ExternFunction equal;
schir::ExternFunction eqv;
schir::ExternFunction call_cc;
schir::ExternFunction values;
schir::ExternFunction call_with_values;
schir::ExternFunction with_exception_handler;
schir::ExternFunction raise;
schir::ExternFunction error;
schir::ExternFunction dynamic_wind;
schir::ExternFunction load_module;
schir::ExternFunction source_loc;
schir::ExternFunction source_loc_valid;
schir::ExternFunction dump_source_loc;
schir::ExternFunction make_syntactic_closure;
schir::ExternFunction load_plugin;
schir::ExternFunction load_builtin;

schir::ExternFunction eval;
schir::ExternFunction op_eval;
schir::ExternFunction compile;
schir::ContextLocal   include_paths;


// Type predicates
schir::ExternFunction is_boolean;
schir::ExternFunction is_bytevector;
schir::ExternFunction is_char;
schir::ExternFunction is_eof_object;
schir::ExternFunction is_null;
schir::ExternFunction is_number;
schir::ExternFunction is_pair;
schir::ExternFunction is_port;
schir::ExternFunction is_procedure;
schir::ExternFunction is_string;
schir::ExternFunction is_symbol;
schir::ExternFunction is_vector;
// Extended types.
schir::ExternFunction is_mlir_operation;
schir::ExternFunction is_source_value;

}

bool SCHIR_BASE_IS_LOADED = false;

namespace schir::builtins {
// See Quasiquote.cpp
mlir::Value quote(OpGen& OG, Pair* P);
mlir::Value quasiquote(OpGen& OG, Pair* P);

// See OpEval.cpp
void op_eval(Context& C, ValueRefs Args);

mlir::Value define(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  Value Id;

  if (!P2)
    return OG.SetError("invalid syntax for define", P);
  if (Pair* LambdaSpec = dyn_cast<Pair>(P2->Car))
    Id = LambdaSpec->Car;
  else
    Id = P2->Car;

  if (!isIdentifier(Id))
    return OG.SetError("invalid syntax for define", P);
  return OG.createDefine(Id, P2, P);
}

// Binds a dynamically loaded schir::ContextLocal.
mlir::Value define_binding(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  Value Name;
  Value ExtName;

  if (P2) {
    Name = P2->Car;
    ExtName = P2->Cdr.car();
  }
  if (!P2 || !ExtName || !isa<Symbol>(Name) || !isa<Symbol, String>(ExtName))
    return OG.SetError("invalid syntax for define-binding", P);
  return OG.createExternalBinding(Name, ExtName);
}

mlir::Value define_syntax(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (!P2) return OG.SetError("invalid define-syntax syntax", P);
  Value Id = P2->Car;
  if (!isIdentifier(Id))
    return OG.SetError("expecting name for define-syntax", P);

  return OG.createSyntaxSpec(P2, P);
}

namespace {
std::pair<schir::Value, schir::Pair*> DestructureSyntaxSpec(OpGen& OG, Pair* P) {
  schir::Value Keyword = P->Car;
  schir::Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (P2) {
    schir::Pair* Spec = dyn_cast<Pair>(P2->Car);
    if (Spec) {
      if (schir::Pair* SpecInput = dyn_cast<Pair>(Spec->Cdr))
        return {Keyword, SpecInput};
    }
  }
  return {};
}
}  // end anon namespace

mlir::Value syntax_rules(OpGen& OG, Pair* P) {
  auto [Keyword, SpecInput] = DestructureSyntaxSpec(OG, P);

  if (!Keyword || !SpecInput)
    return OG.SetError("invalid syntax-rules syntax", P);

  // Check for optional ellipsis identifier.
  Value Ellipsis;
  if (isIdentifier(SpecInput->Car))
    Ellipsis = SpecInput->Car;
  if (Ellipsis) {
    Pair* Temp = dyn_cast<Pair>(SpecInput->Cdr);
    if (!Temp)
      return OG.SetError("invalid syntax-rules syntax.", SpecInput);
    SpecInput = Temp;
  } else {
    Ellipsis = OG.getContext().CreateSymbol("...");
  }
  return OG.createSyntaxRules(P->getSourceLocation(), Keyword, Ellipsis,
                              SpecInput->Car, SpecInput->Cdr);
}

// Convert lambda syntax into schir::Syntax function
// (for use with define-syntax.)
mlir::Value syntax_fn(OpGen& OG, Pair* P) {
  auto [Keyword, SpecInput] = DestructureSyntaxSpec(OG, P);
  if (!Keyword || !SpecInput || !isa<Empty>(SpecInput->Cdr))
    return OG.SetError("invalid syntax for syntax-fn", P);
  schir::SourceLocation Loc = P->getSourceLocation();
  schir::Value ProcExpr = SpecInput->Car;
  schir::FuncOp FuncOp = OG.createSyntaxFunction(Loc, ProcExpr);
  return OG.create<schir::SyntaxOp>(Loc, FuncOp.getSymName());
}

mlir::Value lambda(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (!P2)
    return OG.SetError("invalid lambda syntax", P);
  Value Formals = P2->Car;
  Pair* Body = dyn_cast<Pair>(P2->Cdr);

  if (!Body)
    return OG.SetError("lambda syntax requires body");

  return OG.createLambda(Formals, Body, P->getSourceLocation());
}

mlir::Value if_(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (!P2) return OG.SetError("invalid if syntax", P);
  Value CondExpr = P2->Car;
  P2 = dyn_cast<Pair>(P2->Cdr);
  if (!P2) return OG.SetError("invalid if syntax", P);
  Value ThenExpr = P2->Car;

  Value ElseExpr = nullptr;
  if (isa<Empty>(P2->Cdr))
    ElseExpr = Undefined();
  else if (Pair* P3 = dyn_cast<Pair>(P2->Cdr);
             P3 && isa<Empty>(P3->Cdr))
    ElseExpr = P3->Car;
  if (!ElseExpr)
    return OG.SetError("invalid if syntax", P);

  return OG.createIf(P->getSourceLocation(), CondExpr,
                     ThenExpr, ElseExpr);
}

mlir::Value set(OpGen& OG, Pair* P) {
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (!P2) return OG.SetError("invalid set syntax", P);

  Value S = P2->Car;

  if (!isa<Binding>(S) && !isIdentifier(S))
    return OG.SetError("expecting identifier", P2);

  P2 = dyn_cast<Pair>(P2->Cdr);
  if (!P2) return OG.SetError("invalid set syntax", P2);
  schir::Value Expr = P2->Car;
  if (!isa<Empty>(P2->Cdr)) return OG.SetError("invalid set syntax", P2);
  return OG.createSet(P->getSourceLocation(), S, Expr);
}

mlir::Value syntax_error(OpGen& OG, Pair* P) {
  schir::SourceLocation Loc = P->getSourceLocation();
  Pair* P2 = dyn_cast<Pair>(P->Cdr);
  if (!P2)
    return OG.SetError("invalid syntax-error... syntax", P);
  llvm::SmallVector<mlir::Value, 8> Args;
  for (auto [Loc, V] : WithSource(P2))
    Args.push_back(OG.createLiteral(Loc, V));
  return OG.createError(Loc, Args);
}

mlir::Value case_lambda(OpGen& OG, Pair* P) {
  return OG.createCaseLambda(P);
}

namespace {
  void import_helper(Context& C, ValueRefs Args) {
    if (Pair* P = dyn_cast<Pair>(Args[0])) {
      C.PushCont([](Context& C, ValueRefs) {
        Value RecurseArgs = C.getCapture(0);
        import_helper(C, RecurseArgs);
      }, CaptureList{P->Cdr});
      C.PushCont([](Context& C, ValueRefs Args) {
        auto* ImpSet = cast<ImportSet>(Args[0]);
        C.Import(ImpSet);
      }, CaptureList{});
      C.CreateImportSet(P->Car);
    } else if (isa<Empty>(Args[0])) {
      C.Cont();
    } else {
      C.OpGen->SetError("expecting proper list for import syntax", Args[0]);
    }
  }
}
void import_(Context& C, ValueRefs Args) {
  Value ImportSpecs = cast<Pair>(Args[0])->Cdr;
  if (C.OpGen->isLibraryContext()) {
    C.OpGen->WithLibraryEnv(C.CreateLambda([](Context& C, ValueRefs) {
      Value ImportSpecs = C.getCapture(0);
      import_helper(C, ImportSpecs);
    }, CaptureList{ImportSpecs}));
  } else {
    import_helper(C, ImportSpecs);
  }
}

void export_(Context& C, ValueRefs Args) {
  if (!C.OpGen->isLibraryContext()) {
    return C.RaiseError("export must be in library context", Args[0]);
  }

  Value Input = cast<Pair>(Args[0])->Cdr;

  C.OpGen->WithLibraryEnv(C.CreateLambda([](Context& C, ValueRefs Args) {
    C.OpGen->Export(C.getCapture(0));
  }, CaptureList{Input}));
}

void define_library(Context& C, ValueRefs Args) {
  OpGen& OG = *C.OpGen;
  Pair* P = cast<Pair>(Args[0]);
  auto Loc = P->getSourceLocation();
  if (!OG.isTopLevel() || OG.isLibraryContext()) {
    C.OpGen->SetError("unexpected library definition", P);
    return;
  }
  Value NameSpec;
  Pair* LibraryDecls;
  if (Pair* P2 = dyn_cast<Pair>(P->Cdr)) {
    NameSpec = P2->Car;
    LibraryDecls = dyn_cast<Pair>(P2->Cdr);
    if (!LibraryDecls) {
      C.OpGen->SetError("expected library declarations", P2->Cdr);
      return;
    }
  }
  std::string MangledNameStr = OG.mangleModule(NameSpec);
  if (MangledNameStr.size() == 0) {
    C.OpGen->SetError("library name is invalid", NameSpec);
    return;
  }
  Symbol* MangledName = C.CreateSymbol(MangledNameStr);
  OG.VisitLibrary(Loc, MangledName, LibraryDecls);
}

mlir::Value cond_expand(OpGen& OG, Pair* P) {
  return OG.SetError("TODO cond_expand", P);
}

namespace {
  void handleSequence(Context&C, schir::SourceLocation Loc,
                                 schir::Value Sequence) {
    OpGen& OG = *C.OpGen;
    if (OG.isTopLevel()) {
      OG.VisitTopLevelSequence(Sequence);
    } else {
      mlir::Value Result = OG.createSequence(Loc, Sequence);
      if (C.OpGen->CheckError()) return;
      C.Cont(OpGen::fromValue(Result));
    }
  }
}

void begin(Context& C, ValueRefs Args) {
  Pair* P = cast<Pair>(Args[0]);
  handleSequence(C, P->getSourceLocation(), P->Cdr);
}

void include_(Context& C, ValueRefs Args) {
  schir::SourceLocation Loc = Args[0].getSourceLocation();
  C.PushCont([Loc](Context& C, ValueRefs Args) {
    handleSequence(C, Loc, Args[0]);
  });

  auto* P = cast<Pair>(Args[0]);
  auto* P2 = cast<Pair>(P->Cdr);

  schir::Value ParseSourceFile = SCHIR_BASE_VAR(parse_source_file).get(C);
  schir::Value SourceVal = C.CreateSourceValue(Loc);
  schir::Value Filename = C.RebuildLiteral(P2->Car);
  std::array<schir::Value, 2> NewArgs = {SourceVal, Filename};
  C.Apply(ParseSourceFile, NewArgs);
}

void include_ci(Context& C, ValueRefs Args) {
  // TODO We need to dynamic-wind and set the
  //      context flag for case insensitive parsing
  //      and lookup. Then do what `include` does.
  C.RaiseError("TODO include-ci", Args[0]);
}

void include_library_declarations(Context& C, ValueRefs Args) {
  C.RaiseError("TODO include-library-declarations", Args[0]);
}

void parse_source_file(Context& C, ValueRefs Args) {
  // The user must use SchirScheme::setParseSourceFileFn
  // to define how source files are loaded and stored.
  C.RaiseError("parse-source-file is undefined");
}

// Return the first valid source location or
// an invalid source location if none exists.
void source_loc(Context& C, ValueRefs Args) {
  schir::SourceLocation Loc;
  // Take the first valid source location.
  for (schir::Value Arg : Args) {
    Loc = Arg.getSourceLocation();
    if (Loc.isValid())
      break;
  }
  C.Cont(C.CreateSourceValue(Loc));
}

// Return the first valid source location or
// #f if none exists.
void source_loc_valid(Context& C, ValueRefs Args) {
  schir::SourceLocation Loc;
  // Take the first valid source location.
  for (schir::Value Arg : Args) {
    Loc = Arg.getSourceLocation();
    if (Loc.isValid())
      break;
  }
  if (!Loc.isValid())
    return C.Cont(Bool(false));
  C.Cont(C.CreateSourceValue(Loc));
}

void dump_source_loc(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (!C.SourceManager)
    return C.RaiseError("source manager not available");

  // TODO
  // This could be generalize to take an optional message to work with
  // errors, warnings, notes, etc.

  schir::SourceLocation Loc = Args[0].getSourceLocation();
  schir::FullSourceLocation SL = C.SourceManager->getFullSourceLocation(Loc);
  if (SL.isValid()) {
    schir::SourceLineContext LineContext = SL.getLineContext();
    llvm::errs() << LineContext.FileName
                 << ':' << LineContext.LineNumber
                 << ':' << LineContext.Column
                 // TODO optional message could go here
                 << '\n'
                 << LineContext.LineRange << '\n';
    // Display the caret pointing to the point of interest.
    for (unsigned i = 1; i < LineContext.Column; i++) {
      llvm::errs() << ' ';
    }
    llvm::errs() << "^\n";
  } else {
    llvm::errs() << "<<INVALID SOURCE LOCATION>>\n";
  }
  C.Cont();
}

} // end of namespace schir::builtins

namespace schir {
struct NumberOp {
  // These operations always mutate the first operand
  struct Add {
    static Int f(Context& C, Int X, Int Y) { return X + Y; }
    static void f(Context& C, Float* X, Value Y) {
      X->Val = llvm::APFloat(
          X->Val.convertToDouble() + Number::getAsDouble(Y));
    }
  };
  struct Sub {
    static Int f(Context& C, Int X, Int Y) { return X - Y; }
    static void f(Context& C, Float* X, Value Y) {
      X->Val = llvm::APFloat(
          X->Val.convertToDouble() - Number::getAsDouble(Y));
    }
  };
  struct Mul {
    static Int f(Context& C, Int X, Int Y) { return X * Y; }
    static void f(Context& C, Float* X, Value Y) {
      X->Val = llvm::APFloat(
          X->Val.convertToDouble() * Number::getAsDouble(Y));
    }
  };
  struct Div {
    static void f(Context& C, Float* X, Value Y) {
      X->Val = llvm::APFloat(
          X->Val.convertToDouble() / Number::getAsDouble(Y));
    }
    static Value f(Context& C, Int X, Int Y) {
      if (Number::isExactZero(Y)) {
        C.RaiseError("divide by exact zero", Value(Y));
        return nullptr;
      }
      // TODO Support exact rational numbers.
      // Treat all division as inexact.
      return C.CreateFloat(double{X + 0.0} / Y);
    }
  };
};

} // end namespace schir

namespace schir::builtins {
void call_cc(Context& C, ValueRefs Args) {
  if (Args.size() != 1) return C.RaiseError("invalid arity");
  C.CallCC(Args[0]);
}

void values(Context& C, ValueRefs Args) {
  C.Cont(Args);
}

void call_with_values(Context& C, ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  schir::Value Producer = Args[0];
  schir::Value Consumer = Args[1];
  C.PushCont(Consumer);
  C.Apply(Producer, {});
}

void dump(Context& C, ValueRefs Args) {
  if (Args.size() != 1) return C.RaiseError("invalid arity");
  Args[0].dump();
  C.Cont(Args[0]);
}

void write(Context& C, ValueRefs Args) {
  // TODO Write to specified output port.
  if (Args.size() == 2) return C.RaiseError("port argument unsupported");
  if (Args.size() != 1) return C.RaiseError("invalid arity");
  // TODO Write to specified output port.
  schir::write(llvm::outs(), Args[0]);
  C.Cont(schir::Undefined());
}

void newline(Context& C, ValueRefs Args) {
  // TODO Write to specified output port.
  if (Args.size() == 1) {
    return C.RaiseError("port argument unsupported");
  } else if (Args.size() != 0) {
    return C.RaiseError("invalid arity");
  }

  // schir::write(llvm::outs(), schir::Char('\n'));
  // If output port wraps an llvm::ostream then this would be fine.
  llvm::outs() << '\n';
  C.Cont(schir::Undefined());
}

void string_append(Context& C, ValueRefs Args) {
  size_t TotalLength = 0;
  for (Value Arg : Args) {
    if (!isa<String, Symbol>(Arg))
      return C.RaiseError("expecting string-like object");
    TotalLength += Arg.getStringRef().size();
  }
  String* TargetStr = C.CreateString(TotalLength, '\0');
  llvm::MutableArrayRef<char> TargetRef = TargetStr->getMutableView();
  auto TargetItr = TargetRef.begin();
  auto TargetEndItr = TargetRef.end();
  for (Value Arg : Args) {
    llvm::StringRef CurRef = Arg.getStringRef();
    size_t Dist = std::distance(CurRef.begin(), CurRef.end());
    assert((TargetItr + Dist <= TargetEndItr) && "out of bounds copy");
    std::copy(CurRef.begin(), CurRef.end(), TargetItr);
    TargetItr += Dist;
  }
  C.Cont(TargetStr);
}

void string_length(Context& C, ValueRefs Args) {
  if (Args.size() != 1 || !isa<String, Symbol>(Args[0]))
    return C.RaiseError("expecting string-like object");

  auto Utf8View = detail::Utf8View(Args[0].getStringRef());

  int Size = 0;
  while (Utf8View.drop_front())
    ++Size;

  return C.Cont(Int(Size));
}

void string_ref(Context& C, ValueRefs Args) {
  size_t K = 0;
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  if (!isa<String, Symbol>(Args[0]))
    return C.RaiseError("expecting string-like object");
  if (Args.size() == 2) {
    if (isa<Int>(Args[1]) && cast<Int>(Args[1]) >= 0)
      K = static_cast<size_t>(cast<Int>(Args[1]));
    else
      return C.RaiseError("expecting positive integer");
  }

  auto Utf8View = detail::Utf8View(Args[0].getStringRef());

  Value V;
  for (size_t I = 0; I <= K; ++I) {
    V = Utf8View.drop_front();
    if (!V) {
      return C.RaiseError("invalid index for string: {0} {1}",
                          {Args[1], Args[0]});
    }
  }

  return C.Cont(V);
}

void string_copy(Context& C, ValueRefs Args) {
  if (Args.size() > 3)
    return C.RaiseError("invalid arity");
  if (Args.size() < 1 || !isa<String, Symbol>(Args[0]))
    return C.RaiseError("expecting string-like object");

  size_t StartPos = 0;
  size_t EndPos = ~size_t(0);  // npos

  if (Args.size() >= 2) {
    if (isa<Int>(Args[1]) && cast<Int>(Args[1]) >= 0)
      StartPos = static_cast<size_t>(cast<Int>(Args[1]));
    else
      return C.RaiseError("expecting positive integer");
  }
  if (Args.size() == 3) {
    if (isa<Int>(Args[2]) && cast<Int>(Args[2]) >= 0)
      EndPos = static_cast<size_t>(cast<Int>(Args[2]));
    else
      return C.RaiseError("expecting positive integer");
  }

  auto View = detail::Utf8View(Args[0].getStringRef());
  size_t I = 0;
  for (; I < StartPos; ++I)
    View.drop_front();
  auto Back = View;
  llvm::StringRef ViewStr = View.Range;
  if (EndPos != ~size_t(0)) {
    for (; I < EndPos; ++I)
      Back.drop_front();
    ViewStr = ViewStr.drop_back(Back.Range.size());
  }

  return C.Cont(C.CreateString(ViewStr));
}

void number_to_string(Context& C, ValueRefs Args) {
  if (Args.size() != 1 ||
      !isa<schir::Int, schir::Float>(Args[0]))
    return C.RaiseError("expecting number");
  std::string Str;
  llvm::raw_string_ostream Stream(Str);
  write(Stream, Args[0]);
  C.Cont(C.CreateString(llvm::StringRef(Str)));
}

template <typename Op>
schir::Value arithmetic_reduce(Context& C, Value Accum, ValueRefs Args) {
  for (schir::Value X : Args) {
    if (C.CheckNumber(X)) return nullptr;
    switch (Number::CommonKind(Accum, X)) {
      case ValueKind::Float: {
        // We can assume Float or Int for both values.
        // Make Accum a Float if it is not.
        if (!isa<Float>(Accum)) {
          Accum = C.CreateFloat(double{cast<Int>(Accum) + 0.0});
        }
        Op::f(C, cast<Float>(Accum), X);
        break;
      }
      case ValueKind::Int: {
        // We can assume they are both Int.
        Accum = Op::f(C, cast<Int>(Accum), cast<Int>(X));
        // The result might NOT be Int though.
        if (Accum == nullptr) return nullptr;
        break;
      }
      default:
        // CommonKind had an invalid number type
        // or an error was thrown.
        return nullptr;
    }
  }
  return Accum;
}

void add(Context& C, ValueRefs Args) {
  Value Result = arithmetic_reduce<NumberOp::Add>(C, Int{0}, Args);
  if (Result) {
    C.Cont(Result);
  }
}

void mul(Context&C, ValueRefs Args) {
  Value Result = arithmetic_reduce<NumberOp::Mul>(C, Int{1}, Args);
  if (Result) {
    C.Cont(Result);
  }
}

void sub(Context&C, ValueRefs Args) {
  Value Initial;

  if (Args.size() > 1) {
    Initial = Args[0];
    Args = Args.drop_front();
  } else {
    // Additive inverse
    Initial = Int{0};
  }

  Value Result = arithmetic_reduce<NumberOp::Sub>(C, Initial, Args);
  if (Result) {
    C.Cont(Result);
  }
}

void div(Context& C, ValueRefs Args) {
  Value Initial;

  if (Args.size() > 1) {
    Initial = Args[0];
    Args = Args.drop_front();
  } else {
    // Multiplicative inverse
    Initial = Int{1};
  }
  Value Result = arithmetic_reduce<NumberOp::Div>(C, Initial, Args);
  if (Result) {
    C.Cont(Result);
  }
}

void is_positive(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  Value Input = Args[0];
  if (!isa<Int, Float>(Input))
    return C.RaiseError("expecting number");

  C.Cont(Bool(Number::getAsDouble(Input) > 0.0));
}

void is_zero(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  Value Input = Args[0];
  if (!isa<Int, Float>(Input))
    return C.RaiseError("expecting number");

  C.Cont(Bool(Number::getAsDouble(Input) == 0.0));
}

void equal(Context& C, ValueRefs Args) {
  if (Args.size() != 2) return C.RaiseError("invalid arity");
  Value V1 = Args[0];
  Value V2 = Args[1];
  C.Cont(Bool(::schir::equal(V1, V2)));
}

void eqv(Context& C, ValueRefs Args) {
  if (Args.size() != 2) return C.RaiseError("invalid arity");
  Value V1 = Args[0];
  Value V2 = Args[1];
  C.Cont(Bool(::schir::eqv(V1, V2)));
}

void list(Context& C, ValueRefs Args) {
  // Returns a *newly allocated* list of its arguments.
  C.Cont(C.CreateList(Args));
}

// Return length of proper list.
// Return negative value for circular lists.
// Return nullptr for improper list.
Value length_helper(Value Cur) {
  Value CurFast = Cur;
  int32_t Count = 0;
  while (!isa<Empty>(Cur)) {
    Pair* P1 = dyn_cast<Pair>(Cur);
    Pair* P2 = dyn_cast_or_null<Pair>(CurFast);
    if (!P1)
      return Value();

    Cur = P1->Cdr;
    CurFast = P2 ? P2->Cdr.cdr() : nullptr;

    // TODO Return length of cycle as negative integer.
    if (Cur == CurFast)
      return Value(Int(-1));

    ++Count;
  }

  return Int{Count};
}

// Return negative number if list is circular.
void length(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  Value Result = length_helper(Args[0]);
  if (!Result)
    return C.RaiseError("expecting a list: {}", Args[0]);
  C.Cont(Result);
}

void cons(Context& C, ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  C.Cont(C.CreatePair(Args[0], Args[1]));
}

// (source-cons car cdr src)
// Where `src` can be any value that might
// have a valid source location.
void source_cons(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");
  C.Cont(C.CreatePair(Args[0], Args[1], Args[2]));
}

void car(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (auto* P = dyn_cast<schir::Pair>(Args[0]))
    return C.Cont(P->Car);

  return C.RaiseError("expecting pair: {}", Args[0]);
}

void cdr(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  if (auto* P = dyn_cast<schir::Pair>(Args[0]))
    return C.Cont(P->Cdr);

  return C.RaiseError("expecting pair: {}", Args[0]);
}

namespace {
Value append_rec(Context& C, Value List, Value Cdr) {
  C.setLoc(List);
  if (isa<Empty>(List))
    return Cdr;
  if (Pair* P = dyn_cast<Pair>(List)) {
    if (Value V = append_rec(C, P->Cdr, Cdr))
      return C.CreatePair(P->Car, V, P);
  }
  return Value();
}
}

void append(Context& C, ValueRefs Args) {
  Value NewList = Args.empty() ? Empty() : Args.back();
  Args = Args.drop_back(1);
  for (auto Itr = Args.rbegin(); Itr != Args.rend(); ++Itr) {
    NewList = append_rec(C, *Itr, NewList);
    if (NewList == Value())
      return C.RaiseError("expecting proper list");
  }
  C.Cont(NewList);
}

void make_list(Context& C, ValueRefs Args) {
  if (Args.size() < 1 || Args.size() > 2)
    return C.RaiseError("invalid arity");

  Value Length = Args[0];
  if (!isa<Int>(Length) || cast<Int>(Length) < 0)
    return C.RaiseError("expecting positive integer");

  Value Default = Args.size() == 2 ? Args[1] : Value(Undefined());

  unsigned K = static_cast<unsigned>(cast<Int>(Length));
  Value Result = Empty();
  for (unsigned I = 0; I < K; I++)
    Result = C.CreatePair(Default, Result);

  C.Cont(Result);
}

void list_ref(Context& C, ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");

  Value List = Args[0];
  Value Index = Args[1];

  if (!isa<Pair>(List))
    return C.RaiseError("expecting non-empty list");

  int Len = 0;
  if (Value Length = length_helper(List))
    Len = cast<Int>(Length);
  else
    return;

  int K = 0;
  if (!isa<Int>(Index) || cast<Int>(Index) < 0)
    return C.RaiseError("expecting positive integer");
  K = cast<Int>(Index);

  if (Len >= 0 && K >= Len)
    return C.RaiseError("index is out of range");

  Pair* P = cast<Pair>(List);
  for (int I = 0; I < K; I++)
    P = cast<Pair>(P->Cdr);

  C.Cont(P->Car);
}

void list_set(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");

  Value List = Args[0];
  Value Index = Args[1];
  Value Obj = Args[2];

  if (!isa<Pair>(List))
    return C.RaiseError("expecting non-empty list");

  int Len = 0;
  if (Value Length = length_helper(List))
    Len = cast<Int>(Length);
  else
    return;

  int K = 0;
  if (!isa<Int>(Index) || cast<Int>(Index) < 0)
    return C.RaiseError("expecting positive integer");
  K = cast<Int>(Index);

  // If Len is negative it has infinite length.
  if (Len >= 0 && K >= Len)
    return C.RaiseError("index is out of range");

  Pair* P = cast<Pair>(List);
  for (int I = 0; I < K; I++)
    P = cast<Pair>(P->Cdr);
  P->Car = Obj;
  C.Cont();
}

void vector(Context& C, ValueRefs Args) {
  C.Cont(C.CreateVector(Args));
}

void vector_length(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  Vector* Vec = dyn_cast<Vector>(Args[0]);
  if (!Vec)
    return C.RaiseError("expecting a vector");

  unsigned Len = Vec->getElements().size();
  Value Length = Int(static_cast<int32_t>(Len));

  C.Cont(Length);
}

void make_vector(Context& C, ValueRefs Args) {
  if (Args.size() < 1 || Args.size() > 2)
    return C.RaiseError("invalid arity");

  Value Length = Args[0];
  if (!isa<Int>(Length) || cast<Int>(Length) < 0)
    return C.RaiseError("expecting positive integer");

  Value Default = Args.size() == 2 ? Args[1] : Value(Undefined());

  unsigned K = static_cast<unsigned>(cast<Int>(Length));
  Vector* Vec = C.CreateVector(K, Default);

  C.Cont(Vec);
}

void vector_ref(Context& C, ValueRefs Args) {
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");

  Vector* Vec = dyn_cast<Vector>(Args[0]);
  Value Index = Args[1];

  if (!Vec)
    return C.RaiseError("expecting a vector");

  ValueRefs Xs = Vec->getElements();

  unsigned K = 0;
  if (!isa<Int>(Index) || cast<Int>(Index) < 0)
    return C.RaiseError("expecting positive integer");
  K = static_cast<unsigned>(cast<Int>(Index));

  if (K >= Xs.size())
    return C.RaiseError("index is out of range");

  Value Result = Xs[K];
  C.Cont(Result);
}

void vector_set(Context& C, ValueRefs Args) {
  if (Args.size() != 3)
    return C.RaiseError("invalid arity");

  Vector* Vec = dyn_cast<Vector>(Args[0]);
  if (!Vec)
    return C.RaiseError("expecting vector");
  ValueRefs Xs = Vec->getElements();

  Value Index = Args[1];
  if (!isa<Int>(Index) || cast<Int>(Index) < 0)
    return C.RaiseError("expecting positive integer");
  unsigned K = static_cast<unsigned>(cast<Int>(Index));

  if (K >= Xs.size())
    return C.RaiseError("index out of range");


  Xs[K] = Args[2];
  C.Cont();
}

void with_exception_handler(Context& C, ValueRefs Args) {
  if (Args.size() != 2) return C.RaiseError("invalid arity");
  C.WithExceptionHandler(Args[0], Args[1]);
}

void raise(Context& C, ValueRefs Args) {
  if (Args.size() != 1) return C.RaiseError("invalid arity");
  C.Raise(Args[0]);
}

void error(Context& C, ValueRefs Args) {
  if (Args.size() == 0) return C.RaiseError("invalid arity");
  if (C.CheckKind<String>(Args[0])) return;
  C.RaiseError(cast<String>(Args[0]), Args.drop_front());
}

void dynamic_wind(Context& C, ValueRefs Args) {
  if (Args.size() != 3) return C.RaiseError("invalid arity");
  C.DynamicWind(Args[0], Args[1], Args[2]);
}

// Dynamically and idempotently initialize a module by its mangled name.
void load_module(Context& C, ValueRefs Args) {
  if (Args.size() != 1) return C.RaiseError("invalid arity");
  schir::Symbol* MangledName = dyn_cast<schir::Symbol>(Args[0]);
  if (!MangledName)
    return C.RaiseError("module name should be a symbol");
  C.PushCont([](Context& C, ValueRefs) {
      schir::Symbol* MangledName = cast<Symbol>(C.getCapture(0));
      C.InitModule(MangledName);
  }, CaptureList{MangledName});
  C.LoadModule(MangledName);
}

void eval(Context& C, ValueRefs Args) {
  if (Args.size() < 1)
    return C.RaiseError("invalid arity");

  Value ExprOrDef       = Args[0];
  Value EnvSpec         = Args.size() > 1 ? Args[1] : Value(Undefined());
  Value EvalRaw = Args.size() == 3 ? Args[2] : SCHIR_BASE_VAR(op_eval);

  Value NewArgs[] = {ExprOrDef, EnvSpec, EvalRaw};
  C.Apply(SCHIR_BASE_VAR(compile), NewArgs);
}

void compile(Context& C, ValueRefs Args) {
  if (Args.size() < 1) {
    return C.RaiseError("invalid arity");
  }
  Value ExprOrDef       = Args[0];
  Value EnvSpec         = Args.size() > 1 ? Args[1] : Value(Undefined());
  Value TopLevelHandler = Args.size() > 2 ? Args[2] : Value(Undefined());

  // EnvPtr - Manage ownership of the Environment
  //          if we have to create it on the fly.
  std::unique_ptr<Environment> EnvPtr = nullptr;
  Environment* Env = nullptr;;

  if (auto* E = dyn_cast<Environment>(EnvSpec)) {
    Env = E;
  } else if (isa<ImportSet>(EnvSpec)) {
    auto EnvPtr = std::make_unique<Environment>(C);
    Env = EnvPtr.get();
  } else {
    C.OpGen->SetError("invalid environment specifier", EnvSpec);
    return;
  }

  // Clear any errors in the compiler too keep the dream alive.
  if (schir::OpGen* OpGen = Env->getOpGen())
    OpGen->ClearError();

  // If there is a provided handler then we save the results in an
  // improper, reversed ordered list (stack) to pass to the handler.
  Value ResultHandler = Undefined();
  if (!isa<Undefined>(TopLevelHandler)) {
    Value Results = C.CreateBinding(Empty());
    ResultHandler = C.CreateLambda([](schir::Context& C, ValueRefs Args) {
      // Push each result to a reverse ordered list (stack).
      assert(!Args.empty() && "compile should call handler with result");
      Value NewResult = Args[0];
      Binding* Results = cast<Binding>(C.getCapture(0));
      // Usually there is only a single result.
      if (isa<Empty>(Results->getValue()))
        Results->setValue(NewResult);
      else
        Results->setValue(C.CreatePair(NewResult, Results->getValue()));
      C.Cont();
    }, CaptureList{Results});
    C.PushCont([](Context& C, ValueRefs) {
      // The compiler should already be cleaned up at this point.
      Value TopLevelHandler = C.getCapture(0);
      Value Results = cast<Binding>(C.getCapture(1))->getValue();
      for (Value Result : Results) {
        if (isa<Empty>(Results))
          break;
        C.PushCont([](Context& C, ValueRefs) {
          Value TopLevelHandler = C.getCapture(0);
          Value Result = C.getCapture(1);
          C.Apply(TopLevelHandler, Result);
        }, CaptureList{TopLevelHandler, Result});
      }
      C.Cont();
    }, CaptureList{TopLevelHandler, Results});
  }

  Value Thunk = C.CreateLambda([](schir::Context& C, ValueRefs) {
    Value EnvSpec = C.getCapture(0);
    C.PushCont([](schir::Context& C, ValueRefs) {
      Value ExprOrDef = C.getCapture(0);
      Value ResultHandler = C.getCapture(1);
      if (!isa<Undefined>(ResultHandler))
        C.OpGen->SetTopLevelHandler(ResultHandler);
      C.OpGen->VisitTopLevel(ExprOrDef); // calls Cont()
    }, C.getCaptures().drop_front());
    if (auto* ImpSet = dyn_cast<ImportSet>(EnvSpec)) {
      C.Import(ImpSet);
    } else {
      C.Cont();
    }
  }, CaptureList{EnvSpec, ExprOrDef, ResultHandler});

  C.WithEnv(std::move(EnvPtr), Env, Thunk);
}

void is_boolean(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Bool>(Args[0])));
}
void is_bytevector(Context& C, ValueRefs Args) {
  return C.RaiseError("bytevector not supported");
}
void is_char(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Char>(Args[0])));
}
void is_eof_object(Context& C, ValueRefs Args) {
  return C.RaiseError("eof-object not supported");
}
void is_null(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Empty>(Args[0])));
}
void is_number(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Int, schir::Float>(Args[0])));
}
void is_pair(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Pair, schir::PairWithSource>(Args[0])));
}
void is_port(Context& C, ValueRefs Args) {
  return C.RaiseError("port not supported");
}
void is_procedure(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Lambda, schir::Builtin>(Args[0])));
}
void is_string(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::String>(Args[0])));
}
void is_symbol(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Symbol>(Args[0])));
}
void is_vector(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::Vector>(Args[0])));
}
// Extended types.
void is_mlir_operation(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<mlir::Operation>(Args[0])));
}

void is_source_value(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");
  C.Cont(Bool(isa<schir::SourceValue>(Args[0])));
}

void apply(Context& C, ValueRefs Args) {
  llvm::SmallVector<schir::Value, 8> NewArgs;

  if (Args.size() == 1)
    return C.Apply(Args[0], NewArgs);
  if (Args.size() < 2)
    return C.RaiseError("invalid arity");

  Value Fn = Args.front();
  Value LastArg = Args.back();
  Args = Args.drop_front().drop_back();

  for (schir::Value Arg : Args)
    NewArgs.push_back(Arg);
  for (schir::Value Arg : LastArg)
    NewArgs.push_back(Arg);

  C.Apply(Fn, NewArgs);
}

void make_syntactic_closure(Context& C, ValueRefs Args) {
  llvm_unreachable("make-syntactic-closure is not supported");
  if (Args.size() != 2)
    return C.RaiseError("invalid arity");
  Value Expr = Args[0];
  Value Env = Args[1];
  schir::SourceLocation Loc = Expr.getSourceLocation();
  Value Result = C.CreateSyntaxClosure(Loc, Expr, Env);
  C.Cont(Result);
}

// Dynamically load a native shared library.
void load_plugin(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  llvm::StringRef Filename = Args.front().getStringRef();
  if (Filename.empty())
    return C.RaiseError("expecting nonempty string-like object: {}",
                        Args.front());

  std::string ErrMsg;
  if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Filename.data(),
                                                        &ErrMsg)) {
    // TODO Make a "note" with Msg.
    String* Msg = C.CreateString(ErrMsg);
    return C.RaiseError("Failed to load plugin: {}\n{}", {Args.front(), Msg});
  }

  C.Cont();
}

// Dynamically load a Builtin from a schir::ValueFn.
void load_builtin(Context& C, ValueRefs Args) {
  if (Args.size() != 1)
    return C.RaiseError("invalid arity");

  llvm::StringRef FuncName = Args.front().getStringRef();
  if (FuncName.empty())
    return C.RaiseError("expecting nonempty string-like object: {}",
                        Args.front());

  void* FuncVoidPtr
    = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(FuncName.data());
  if (FuncVoidPtr == nullptr)
    return C.RaiseError("unable to load builtin: {}", Args.front());
  schir::ValueFn Fn = reinterpret_cast<schir::ValueFn>(FuncVoidPtr);
  C.Cont(C.CreateBuiltin(Fn));
}
} // end of namespace schir::builtins

// initialize the module for run-time independent of the compiler
void SCHIR_BASE_INIT(schir::Context& Context) {
  Context.DialectRegistry->insert<schir::SchirDialect>();
  // syntax
  SCHIR_BASE_VAR(define)          = schir::builtins::define;
  SCHIR_BASE_VAR(define_binding)  = schir::builtins::define_binding;
  SCHIR_BASE_VAR(define_syntax)   = schir::builtins::define_syntax;
  SCHIR_BASE_VAR(syntax_rules)    = schir::builtins::syntax_rules;
  SCHIR_BASE_VAR(syntax_fn)       = schir::builtins::syntax_fn;
  SCHIR_BASE_VAR(syntax_error)    = schir::builtins::syntax_error;
  SCHIR_BASE_VAR(if_)             = schir::builtins::if_;
  SCHIR_BASE_VAR(lambda)          = schir::builtins::lambda;
  SCHIR_BASE_VAR(case_lambda)     = schir::builtins::case_lambda;
  SCHIR_BASE_VAR(quasiquote)      = schir::builtins::quasiquote;
  SCHIR_BASE_VAR(quote)           = schir::builtins::quote;
  SCHIR_BASE_VAR(set)             = schir::builtins::set;
  SCHIR_BASE_VAR(begin)           = schir::builtins::begin;
  SCHIR_BASE_VAR(cond_expand)     = schir::builtins::cond_expand;
  SCHIR_BASE_VAR(define_library)  = schir::builtins::define_library;
  SCHIR_BASE_VAR(export_)         = schir::builtins::export_;
  SCHIR_BASE_VAR(include)         = schir::builtins::include_;
  SCHIR_BASE_VAR(include_ci)      = schir::builtins::include_ci;
  SCHIR_BASE_VAR(include_library_declarations)
    = schir::builtins::include_library_declarations;


  SCHIR_BASE_VAR(source_loc)      = schir::builtins::source_loc;
  SCHIR_BASE_VAR(source_loc_valid) = schir::builtins::source_loc_valid;
  SCHIR_BASE_VAR(dump_source_loc) = schir::builtins::dump_source_loc;

  // functions
  SCHIR_BASE_VAR(add)     = schir::builtins::add;
  SCHIR_BASE_VAR(sub)     = schir::builtins::sub;
  SCHIR_BASE_VAR(div)     = schir::builtins::div;
  SCHIR_BASE_VAR(mul)     = schir::builtins::mul;
  SCHIR_BASE_VAR(is_positive) = schir::builtins::is_positive;
  SCHIR_BASE_VAR(is_zero) = schir::builtins::is_zero;
  SCHIR_BASE_VAR(list)    = schir::builtins::list;
  SCHIR_BASE_VAR(length)  = schir::builtins::length;
  SCHIR_BASE_VAR(cons)    = schir::builtins::cons;
  SCHIR_BASE_VAR(source_cons) = schir::builtins::source_cons;
  SCHIR_BASE_VAR(car)     = schir::builtins::car;
  SCHIR_BASE_VAR(cdr)     = schir::builtins::cdr;
  SCHIR_BASE_VAR(append)  = schir::builtins::append;
  SCHIR_BASE_VAR(vector)  = schir::builtins::vector;
  SCHIR_BASE_VAR(make_vector) = schir::builtins::make_vector;
  SCHIR_BASE_VAR(vector_length) = schir::builtins::vector_length;
  SCHIR_BASE_VAR(vector_ref) = schir::builtins::vector_ref;
  SCHIR_BASE_VAR(vector_set) = schir::builtins::vector_set;
  SCHIR_BASE_VAR(make_list) = schir::builtins::make_list;
  SCHIR_BASE_VAR(list_ref) = schir::builtins::list_ref;
  SCHIR_BASE_VAR(list_set) = schir::builtins::list_set;
  SCHIR_BASE_VAR(dump)    = schir::builtins::dump;
  SCHIR_BASE_VAR(write)   = schir::builtins::write;
  SCHIR_BASE_VAR(newline) = schir::builtins::newline;
  SCHIR_BASE_VAR(string_append) = schir::builtins::string_append;
  SCHIR_BASE_VAR(string_copy) = schir::builtins::string_copy;
  SCHIR_BASE_VAR(string_length) = schir::builtins::string_length;
  SCHIR_BASE_VAR(string_ref) = schir::builtins::string_ref;
  SCHIR_BASE_VAR(number_to_string) = schir::builtins::number_to_string;
  SCHIR_BASE_VAR(eq)      = schir::builtins::eqv;
  SCHIR_BASE_VAR(equal)   = schir::builtins::equal;
  SCHIR_BASE_VAR(eqv)     = schir::builtins::eqv;
  SCHIR_BASE_VAR(call_cc) = schir::builtins::call_cc;
  SCHIR_BASE_VAR(values)  = schir::builtins::values;
  SCHIR_BASE_VAR(call_with_values) = schir::builtins::call_with_values;
  SCHIR_BASE_VAR(with_exception_handler)
    = schir::builtins::with_exception_handler;
  SCHIR_BASE_VAR(raise)   = schir::builtins::raise;
  SCHIR_BASE_VAR(error)   = schir::builtins::error;
  SCHIR_BASE_VAR(dynamic_wind) = schir::builtins::dynamic_wind;

  SCHIR_BASE_VAR(eval)    = schir::builtins::eval;
  SCHIR_BASE_VAR(op_eval) = schir::builtins::op_eval;
  SCHIR_BASE_VAR(compile) = schir::builtins::compile;

  SCHIR_BASE_VAR(is_boolean) = schir::builtins::is_boolean;
  SCHIR_BASE_VAR(is_bytevector) = schir::builtins::is_bytevector;
  SCHIR_BASE_VAR(is_char) = schir::builtins::is_char;
  SCHIR_BASE_VAR(is_eof_object) = schir::builtins::is_eof_object;
  SCHIR_BASE_VAR(is_null) = schir::builtins::is_null;
  SCHIR_BASE_VAR(is_number) = schir::builtins::is_number;
  SCHIR_BASE_VAR(is_pair) = schir::builtins::is_pair;
  SCHIR_BASE_VAR(is_port) = schir::builtins::is_port;
  SCHIR_BASE_VAR(is_procedure) = schir::builtins::is_procedure;
  SCHIR_BASE_VAR(is_string) = schir::builtins::is_string;
  SCHIR_BASE_VAR(is_symbol) = schir::builtins::is_symbol;
  SCHIR_BASE_VAR(is_vector) = schir::builtins::is_vector;
  SCHIR_BASE_VAR(is_mlir_operation) = schir::builtins::is_mlir_operation;
  SCHIR_BASE_VAR(is_source_value) = schir::builtins::is_source_value;
  SCHIR_BASE_VAR(apply) = schir::builtins::apply;
  SCHIR_BASE_VAR(make_syntactic_closure)
                        = schir::builtins::make_syntactic_closure;
  SCHIR_BASE_VAR(load_plugin) = schir::builtins::load_plugin;
  SCHIR_BASE_VAR(load_builtin) = schir::builtins::load_builtin;
}

// initializes the module and loads lookup information
// for the compiler
void SCHIR_BASE_LOAD_MODULE(schir::Context& Context) {
  // Note: We call SCHIR_BASE_INIT in the constructor of Context.
  schir::initModuleNames(Context, SCHIR_BASE_LIB_STR, {
    // syntax
    {"define",        SCHIR_BASE_VAR(define)},
    {"define-binding",
                      SCHIR_BASE_VAR(define_binding)},
    {"define-syntax", SCHIR_BASE_VAR(define_syntax)},
    {"if",            SCHIR_BASE_VAR(if_)},
    {"lambda",        SCHIR_BASE_VAR(lambda)},
    {"quasiquote",    SCHIR_BASE_VAR(quasiquote)},
    {"quote",         SCHIR_BASE_VAR(quote)},
    {"set!",          SCHIR_BASE_VAR(set)},
    {"syntax-rules",  SCHIR_BASE_VAR(syntax_rules)},
    {"syntax-fn",     SCHIR_BASE_VAR(syntax_fn)},
    {"syntax-error",  SCHIR_BASE_VAR(syntax_error)},
    {"case-lambda",   SCHIR_BASE_VAR(case_lambda)},
    {"begin",         SCHIR_BASE_VAR(begin)},
    {"cond-expand",   SCHIR_BASE_VAR(cond_expand)},
    {"define-library",SCHIR_BASE_VAR(define_library)},
    {"export",        SCHIR_BASE_VAR(export_)},
    {"include",       SCHIR_BASE_VAR(include)},
    {"include-ci",    SCHIR_BASE_VAR(include_ci)},
    {"include-library-declarations",
      SCHIR_BASE_VAR(include_library_declarations)},

    {"source-loc",    SCHIR_BASE_VAR(source_loc)},
    {"source-loc-valid", SCHIR_BASE_VAR(source_loc_valid)},
    {"dump-source-loc", SCHIR_BASE_VAR(dump_source_loc)},
    {"parse-source-file",
                      SCHIR_BASE_VAR(parse_source_file).get(Context)},

    // functions
    {"+",       SCHIR_BASE_VAR(add)},
    {"-",       SCHIR_BASE_VAR(sub)},
    {"/",       SCHIR_BASE_VAR(div)},
    {"*",       SCHIR_BASE_VAR(mul)},
    {"positive?", SCHIR_BASE_VAR(is_positive)},
    {"zero?", SCHIR_BASE_VAR(is_zero)},
    {"list",    SCHIR_BASE_VAR(list)},
    {"length",  SCHIR_BASE_VAR(length)},
    {"cons",    SCHIR_BASE_VAR(cons)},
    {"source-cons", SCHIR_BASE_VAR(source_cons)},
    {"car",     SCHIR_BASE_VAR(car)},
    {"cdr",     SCHIR_BASE_VAR(cdr)},
    {"append",  SCHIR_BASE_VAR(append)},
    {"vector", SCHIR_BASE_VAR(vector)},
    {"make-vector", SCHIR_BASE_VAR(make_vector)},
    {"vector-length", SCHIR_BASE_VAR(vector_length)},
    {"vector-ref", SCHIR_BASE_VAR(vector_ref)},
    {"vector-set!", SCHIR_BASE_VAR(vector_set)},
    {"make-list", SCHIR_BASE_VAR(make_list)},
    {"list-ref", SCHIR_BASE_VAR(list_ref)},
    {"list-set!", SCHIR_BASE_VAR(list_set)},
    {"dump",    SCHIR_BASE_VAR(dump)},
    {"write",   SCHIR_BASE_VAR(write)},
    {"newline", SCHIR_BASE_VAR(newline)},
    {"string-append", SCHIR_BASE_VAR(string_append)},
    {"string-copy", SCHIR_BASE_VAR(string_copy)},
    {"string-length", SCHIR_BASE_VAR(string_length)},
    {"string-ref", SCHIR_BASE_VAR(string_ref)},
    {"number->string", SCHIR_BASE_VAR(number_to_string)},
    {"eq?",     SCHIR_BASE_VAR(eq)},
    {"equal?",  SCHIR_BASE_VAR(equal)},
    {"eqv?",    SCHIR_BASE_VAR(eqv)},
    {"call/cc", SCHIR_BASE_VAR(call_cc)},
    {"values", SCHIR_BASE_VAR(values)},
    {"call-with-values", SCHIR_BASE_VAR(call_with_values)},
    {"with-exception-handler", SCHIR_BASE_VAR(with_exception_handler)},
    {"raise", SCHIR_BASE_VAR(raise)},
    {"error", SCHIR_BASE_VAR(error)},
    {"dynamic-wind", SCHIR_BASE_VAR(dynamic_wind)},

    {"eval",    SCHIR_BASE_VAR(eval)},
    {"op-eval", SCHIR_BASE_VAR(op_eval)},
    {"compile", SCHIR_BASE_VAR(compile)},
    {"include-paths", SCHIR_BASE_VAR(include_paths).get(Context)},

    // Type predicates
    {"boolean?", SCHIR_BASE_VAR(is_boolean)},
    {"bytevector?", SCHIR_BASE_VAR(is_bytevector)},
    {"char?", SCHIR_BASE_VAR(is_char)},
    {"eof-object?", SCHIR_BASE_VAR(is_eof_object)},
    {"null?", SCHIR_BASE_VAR(is_null)},
    {"number?", SCHIR_BASE_VAR(is_number)},
    {"pair?", SCHIR_BASE_VAR(is_pair)},
    {"port?", SCHIR_BASE_VAR(is_port)},
    {"procedure?", SCHIR_BASE_VAR(is_procedure)},
    {"string?", SCHIR_BASE_VAR(is_string)},
    {"symbol?", SCHIR_BASE_VAR(is_symbol)},
    {"vector?", SCHIR_BASE_VAR(is_vector)},
    // Extended types.
    {"mlir-operation?", SCHIR_BASE_VAR(is_mlir_operation)},
    {"source-value?", SCHIR_BASE_VAR(is_source_value)},
    {"apply", SCHIR_BASE_VAR(apply)},
    {"make-syntactic-closure", SCHIR_BASE_VAR(make_syntactic_closure)},
    {"load-plugin", SCHIR_BASE_VAR(load_plugin)},
    {"load-builtin", SCHIR_BASE_VAR(load_builtin)},
  });
}

namespace schir::detail {
// Borrowed from YAMLParser.
std::pair<uint32_t, unsigned> Utf8View::decode_front() const {
  StringRef::iterator Position= Range.begin();
  StringRef::iterator End = Range.end();
  // 1 byte: [0x00, 0x7f]
  // Bit pattern: 0xxxxxxx
  if (Position < End && (*Position & 0x80) == 0) {
    return std::make_pair(*Position, 1);
  }
  // 2 bytes: [0x80, 0x7ff]
  // Bit pattern: 110xxxxx 10xxxxxx
  if (Position + 1 < End && ((*Position & 0xE0) == 0xC0) &&
      ((*(Position + 1) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x1F) << 6) |
                          (*(Position + 1) & 0x3F);
    if (codepoint >= 0x80)
      return std::make_pair(codepoint, 2);
  }
  // 3 bytes: [0x8000, 0xffff]
  // Bit pattern: 1110xxxx 10xxxxxx 10xxxxxx
  if (Position + 2 < End && ((*Position & 0xF0) == 0xE0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x0F) << 12) |
                         ((*(Position + 1) & 0x3F) << 6) |
                          (*(Position + 2) & 0x3F);
    // Codepoints between 0xD800 and 0xDFFF are invalid, as
    // they are high / low surrogate halves used by UTF-16.
    if (codepoint >= 0x800 &&
        (codepoint < 0xD800 || codepoint > 0xDFFF))
      return std::make_pair(codepoint, 3);
  }
  // 4 bytes: [0x10000, 0x10FFFF]
  // Bit pattern: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  if (Position + 3 < End && ((*Position & 0xF8) == 0xF0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80) &&
      ((*(Position + 3) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x07) << 18) |
                         ((*(Position + 1) & 0x3F) << 12) |
                         ((*(Position + 2) & 0x3F) << 6) |
                          (*(Position + 3) & 0x3F);
    if (codepoint >= 0x10000 && codepoint <= 0x10FFFF)
      return std::make_pair(codepoint, 4);
  }
  return std::make_pair(0, 0);
}

// Borrowed from YAMLParser.
// Encode \a UnicodeScalarValue in UTF-8 and append it to result.
void encode_utf8(uint32_t UnicodeScalarValue,
                 llvm::SmallVectorImpl<char> &Result) {
  if (UnicodeScalarValue <= 0x7F) {
    Result.push_back(UnicodeScalarValue & 0x7F);
  } else if (UnicodeScalarValue <= 0x7FF) {
    uint8_t FirstByte = 0xC0 | ((UnicodeScalarValue & 0x7C0) >> 6);
    uint8_t SecondByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
  } else if (UnicodeScalarValue <= 0xFFFF) {
    uint8_t FirstByte = 0xE0 | ((UnicodeScalarValue & 0xF000) >> 12);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t ThirdByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
    Result.push_back(ThirdByte);
  } else if (UnicodeScalarValue <= 0x10FFFF) {
    uint8_t FirstByte = 0xF0 | ((UnicodeScalarValue & 0x1F0000) >> 18);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0x3F000) >> 12);
    uint8_t ThirdByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t FourthByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
    Result.push_back(ThirdByte);
    Result.push_back(FourthByte);
  }
}

void encode_hex(uint32_t Code, llvm::SmallVectorImpl<char> &Result) {
  llvm::StringRef Digits = "0123456789ABCDEF";
  unsigned Digit = 0;

  // Interval [16^0, 16^8).
  for (unsigned i = 0; i < 8; ++i) {
    Digit = (Code >> (4 * (7 - i))) % 16;
    // Do not push leading zeros.
    if (Digit > 0 || !Result.empty() || i == 7)
      Result.push_back(Digits[Digit]);
  }
}

// Note that we do not validate UTF8 here. (Should we?)
std::pair</*CodePoint=*/uint32_t, /*IsError=*/bool>
from_hex(llvm::StringRef Chars) {
  llvm::StringRef Digits = "0123456789ABCDEF";
  uint32_t Output = 0;

  for (unsigned i = 0; i < Chars.size(); ++i) {
    size_t Digit = Digits.find(llvm::toUpper(Chars[Chars.size() - i - 1]));
    if (Digit == llvm::StringRef::npos)
      return {0, true};
    Output += Digit << (i * 4);
  }

  return {Output, false};
}

} // end of namespace schir::detail
