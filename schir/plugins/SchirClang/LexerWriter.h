// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_LEXER_WRITER_H
#define SCHIRCLANG_LEXER_WRITER_H

#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <llvm/Support/Allocator.h>
#include <algorithm>
#include <memory>

namespace {
  class SchirSchemePragmaHandler;
}

namespace schir_clang {
// It is complicated to keep the TokenBuffer alive
// for the Preprocessor, so we use an array to give
// ownership via the EnterTokenStream overload.
class LexerWriter {
  using Token = clang::Token;
  clang::Parser& Parser;
  llvm::BumpPtrAllocator& LexerSpellings;
  std::unique_ptr<Token[]> TokenBuffer;
  unsigned Capacity = 0;
  unsigned Size = 0;

  void realloc(unsigned NewCapacity) {
    std::unique_ptr<Token[]> NewTokenBuffer(new Token[NewCapacity]());
    if (Capacity > 0)
      std::copy(&TokenBuffer[0], &TokenBuffer[Size],
                NewTokenBuffer.get());
    TokenBuffer = std::move(NewTokenBuffer);
    Capacity = NewCapacity;
  }

  void push_back(Token Tok) {
    unsigned NewSize = Size + 1;
    if (Capacity < NewSize) {
      // Start with a reasonable 128 bytes and then
      // double capacity each time it is needed.
      unsigned NewCapacity = Capacity > 0 ? Capacity * 2 : 128;
      realloc(NewCapacity);
    }
    TokenBuffer[Size] = Tok;
    Size = NewSize;
  }

public:
  LexerWriter(clang::Parser& P,
              llvm::BumpPtrAllocator& LexerSpellings)
    : Parser(P),
      LexerSpellings(LexerSpellings),
      TokenBuffer(nullptr)
  { }

  ~LexerWriter() {
    Capacity = 0;
    Size = 0;
  }

  // Lex tokens from string and push to TokenBuffer.
  // Copy to a std::string to guarantee a null terminator.
  void Tokenize(clang::SourceLocation Loc, llvm::StringRef Chars) {
    if (Chars.empty()) return;
    // Copy to LexerSpellings to ensure null terminator.
    Chars = Chars.copy(LexerSpellings);
    char* NullTerm = LexerSpellings.template Allocate<char>(1);
    *NullTerm = 0;
    // Lex Tokens for the TokenBuffer.
    clang::Lexer Lexer(clang::SourceLocation(), Parser.getLangOpts(),
            Chars.data(), Chars.data(), &(*(Chars.end())));
    while (true) {
      Token Tok;
      Lexer.LexFromRawLexer(Tok);

      // Raw identifiers need to be looked up.
      if (Tok.is(clang::tok::raw_identifier))
        Parser.getPreprocessor().LookUpIdentifierInfo(Tok);

      Tok.setLocation(Loc);
      Tok.setFlag(clang::Token::IsReinjected);

      if (Tok.is(clang::tok::eof)) break;

      push_back(Tok);
    }
  }

  void PushResumeToken(clang::SourceLocation Loc,
                       SchirSchemePragmaHandler* Handler) {
    clang::Token Tok;
    Tok.startToken();
    Tok.setKind(clang::tok::annot_pragma_parse_ext_decl);
    Tok.setLocation(Loc);
    Tok.setAnnotationValue(static_cast<void*>(Handler));
    push_back(Tok);
  }

  // Terminate token stream for temporary parsing.
  void PushEod() {
    assert(Size > 0 && "token buffer should be nonempty");
    clang::Token EndTok;
    EndTok.startToken();
    EndTok.setKind(clang::tok::eod);
    EndTok.setLocation(TokenBuffer[0].getLocation());
    push_back(EndTok);
  }

  // This must be called AFTER we update the Clang Lexer position.
  void FlushTokens() {
    if (Size == 0) return;

    // Note we manually mark tokens as reinjected as required.
    Parser.getPreprocessor().EnterTokenStream(std::move(TokenBuffer), Size,
                    /*DisableMacroExpansion=*/true,
                    /*IsReinject=*/false);
    Capacity = 0;
    Size = 0;
  }
};
} // namespace schir_clang

#endif // SCHIRCLANG_LEXER_WRITER_H
