// Copyright Jason Rice 2026
#ifndef SCHIRCLANG_LEXER_WRITER_H
#define SCHIRCLANG_LEXER_WRITER_H

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

      if (Tok.is(clang::tok::eof)) break;
      push_back(Tok);
    }
  }

#if 0 // TODO REMOVE if not used
  // Clear without flushing.
  void ClearTokens() {
    TokenBuffer.reset();
    Capacity = 0;
    Size = 0;
  }
#endif

  // This must be called AFTER we update the Clang Lexer position.
  void FlushTokens() {
    if (Size == 0) return;
    Parser.getPreprocessor().EnterTokenStream(std::move(TokenBuffer), Size,
                    /*DisableMacroExpansion=*/true,
                    /*IsReinject=*/true);
    Capacity = 0;
    Size = 0;
  }
};
} // namespace schir_clang

#endif // SCHIRCLANG_LEXER_WRITER_H
