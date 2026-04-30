//=== - MangleTest.cpp - Tests for schir::Mangler --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------===//

#include "schir/Context.h"
#include "schir/Mangle.h"

#include <gtest/gtest.h>
#include <string>

namespace {

TEST(MangleTest, mangleLibraryName) {
  auto Context = schir::Context();
  auto Mangler = schir::Mangler(Context);
  auto mangle = [&](llvm::StringRef Name) {
    schir::Value Spec = Context.ParseLiteral(Name);
    return Mangler.mangleModule(Spec);
  };

  EXPECT_EQ(mangle("(schir base)"), "_SCHIRL5SschirL4Sbase");
  EXPECT_EQ(mangle("(schir clang)"), "_SCHIRL5SschirL5Sclang");
  EXPECT_EQ(mangle("(schir mlir)"), "_SCHIRL5SschirL4Smlir");
  EXPECT_EQ(mangle("(schir foo-bar)"), "_SCHIRL5SschirL3Sfoomi3Sbar");
  EXPECT_EQ(mangle("(schir foo.bar 42)"), "_SCHIRL5SschirL3Sfoodt3SbarL2S42");
  EXPECT_EQ(mangle("(foo_bar3)"), "_SCHIRL8Sfoo_bar3");
  EXPECT_EQ(mangle("(foo_bar3 (woof))"), "");
  EXPECT_EQ(mangle("(rmrf *)"), "_SCHIRL4SrmrfLml");
}

TEST(MangleTest, parseLibraryName) {
  auto Context = schir::Context();
  auto Mangler = schir::Mangler(Context);

  std::string MangledNameStr;
  auto mangle = [&](llvm::StringRef Name) {
    schir::Value Spec = Context.ParseLiteral(Name);
    MangledNameStr = Mangler.mangleModule(Spec);
    auto MangledName = llvm::StringRef(MangledNameStr);
    // Strip the _SCHIR prefix.
    MangledName.consume_front(Mangler.getManglePrefix());
    return MangledName;
  };
  llvm::SmallString<32> Output;
  llvm::StringRef MangledName;

  MangledName = mangle("(schir base)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "schir");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "base");
  Output.clear();

  MangledName = mangle("(schir clang)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "schir");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "clang");
  Output.clear();

  MangledName = mangle("(schir foo-bar)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "schir");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "foo-bar");
  Output.clear();

  MangledName = mangle("(schir foo.bar 42)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "schir");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "foo.bar");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "42");
  Output.clear();

  MangledName = mangle("(foo_bar3)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "foo_bar3");
  Output.clear();

  MangledName = mangle("(rmrf *)");
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "rmrf");
  Output.clear();
  EXPECT_TRUE(Mangler.parseLibraryName(MangledName, Output));
  EXPECT_EQ(std::string(Output), "*");
  Output.clear();
}

}
