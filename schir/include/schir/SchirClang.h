// Copyright 2026 Jason Rice

#ifndef SCHIR_SCHIRCLANG_H
#define SCHIR_SCHIRCLANG_H

#include <schir/Context.h>
#include <schir/Source.h>
#include <schir/Value.h>
#include <string>
#include <vector>

namespace schir_clang {
struct SchirClangImpl;

// Provide a public interface for the SchirClang plugin hooks
// to be used by IR Passes and Translaters.
// (Implement in SchirClang.cpp)
class SchirClang {
  SchirClangImpl* Impl = nullptr;

public:
  std::string ErrorMsg = {};

  SchirClang(SchirClangImpl* Impl) : Impl(Impl) { }
  SchirClangImpl& getImpl() const { return *Impl; }

  explicit operator bool() const { return Impl; }
  bool HasError() const { return !ErrorMsg.empty(); }

  // Take a string literal as an error message.
  void SetError(llvm::StringRef Msg) {
    ErrorMsg = Msg;
  }
  void TemplateProbe(llvm::SmallVectorImpl<std::vector<std::string>>& Results,
                     schir::SourceLocation Loc, llvm::StringRef TemplateName,
                     llvm::StringRef Expr);
  schir::Value ExprEval(schir::SourceLocation Loc, llvm::StringRef Expr);
  std::string ExprType(schir::SourceLocation Loc, llvm::StringRef Expr);
  std::string ParseType(schir::SourceLocation Loc, llvm::StringRef TypeStr);
  void WriteLexer(schir::SourceLocation Loc, llvm::StringRef Str);
};

} // namespace schir_clang

#endif // SCHIR_SCHIRCLANG_H
