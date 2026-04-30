//===--------- Parser.h - SchirScheme Language Parser -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Parser interface for SchirScheme.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_PARSER_H
#define LLVM_SCHIR_PARSER_H

#include "schir/Context.h"
#include "schir/Lexer.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace schir {

class ValueResult {
  Value V = nullptr;

public:
  ValueResult() = default;
  ValueResult(Value V)
    : V(V)
  { }

  Value get() {
    assert(isUsable() && "should not get unusable result");
    return V;
  }

  bool isUsable() {
    return V && V.getKind() != ValueKind::Undefined;
  }
};

class Parser {
  using ValueResult = schir::ValueResult;
  using Value = schir::Value;
  schir::Lexer& Lexer;
  schir::Context& Context;
  Token Tok = {};
  TokenKind Terminator = tok::eof;
  bool IsFinished = false;
  SourceLocation PrevTokLocation;
  std::string LiteralResult = {};
  std::string ErrorMsg = {};
  Token ErrTok = {};
  Token CurStartTok = {};

  auto ValueEmpty() { return ValueResult(); }
  auto ValueError() { return ValueResult(Context.CreateUndefined()); }
  bool ParseLiteralImpl();

  ValueResult ParseExpr(Token const& StartTok);
  ValueResult ParseExprAbbrev(Token const& StartTok,
                              char const* Name);

  ValueResult ParseCharConstant();
  ValueResult ParseExternName();
  ValueResult ParseList(Token const& StartTok);
  ValueResult ParseListStart();
  ValueResult ParseNumber();
  ValueResult ParseString();
  ValueResult ParseSymbol();
  ValueResult ParseEscapedSymbol();
  ValueResult ParseVectorStart(bool IsByteVector = false);
  ValueResult ParseVector(Token const& StartTok,
                          llvm::SmallVectorImpl<Value>& Xs,
                          bool IsByteVector);

  ValueResult ParseDottedCdr(Token const& StartTok);
  ValueResult ParseSpecialEscapeSequence();

  ValueResult SetError(Token const& Tok, StringRef Msg) {
    ErrTok = Tok;
    ErrorMsg = Msg;
    // Prevent infinite loops in the absence of error checks.
    ConsumeToken();
    return ValueError();
  }

  bool CheckTerminator() {
    if (Tok.getKind() == Terminator) {
      IsFinished = true;
    }
    return IsFinished;
  }

public:
  Parser(schir::Lexer& Lexer, schir::Context& C)
    : Lexer(Lexer)
    , Context(C)
  { }

  bool isFinished() const {
    return IsFinished;
  }

  bool HasError() const {
    return !ErrorMsg.empty();
  }

  void RaiseError() {
    assert(HasError() && "There must be an error to raise.");
    Token TempTok = ErrTok.getLength() > 0 ? ErrTok : Tok;
    SourceLocation Loc = TempTok.getLocation();
    Context.setLoc(Loc);
    Context.RaiseError(ErrorMsg);
  }

  ValueResult ParseTopLevelExpr();
  ValueResult Parse(schir::TokenKind Term = tok::eof);

  // Consumes the first token for parsing.
  // If the terminator is brace-like, the
  // matching opening brace is expected as
  // the first token and is consumed.
  // Returns false on failure. (sorry)
  bool PrimeToken(schir::TokenKind Term = tok::eof) {
    Lexer.Lex(Tok);

    Terminator = Term;
    switch (Terminator) {
    case tok::r_brace:
      return TryConsumeToken(tok::l_brace, "expecting {");
    case tok::r_paren:
      return TryConsumeToken(tok::l_paren, "expecting (");
    case tok::r_square:
      return TryConsumeToken(tok::l_square, "expecting [");
    default:
      return true;
    }
  }

  SourceLocation ConsumeToken() {
    PrevTokLocation = Tok.getLocation();
    Lexer.Lex(Tok);
    return PrevTokLocation;
  }

  bool TryConsumeToken(schir::TokenKind Expected, llvm::StringRef ErrMsg) {
    if (Tok.isNot(Expected)) {
      SetError(Tok, ErrMsg);
      return false;
    }
    ConsumeToken();
    return true;
  }
};

}  // end namespace schir

#endif
