//=== - ContextTest.cpp - Tests for schir::Context --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------===//

#include "schir/Context.h"
#include "schir/Value.h"

#include <gtest/gtest.h>

namespace {

struct LifetimeTracker {
  bool& IsAlive;

  LifetimeTracker(bool& IsAlive_)
    : IsAlive(IsAlive_)
  {
    IsAlive = true;
  }

  LifetimeTracker(LifetimeTracker const&) = delete;

  ~LifetimeTracker() {
    IsAlive = false;
  }
};

TEST(ContextTest, ParseLiteral) {
  auto Context = schir::Context();
  auto parse = [&](std::string Input) {
    schir::Value Spec = Context.ParseLiteral(Input);
    std::string Str;
    llvm::raw_string_ostream Stream(Str);
    schir::write(Stream, Spec);
    return Str;
  };

  EXPECT_EQ(parse("(schir base)"), "(schir base)");
  EXPECT_EQ(parse("'foo"), "(quote foo)");
  EXPECT_EQ(parse("\"foo\""), "\"foo\"");
}

TEST(ContextTest, DynamicWindNormalExit) {
  auto Context = schir::Context();
  bool IsAlive = false;
  bool Check_1 = false;
  bool Check_2 = false;
  auto TrackerPtr = std::make_unique<LifetimeTracker>(IsAlive);

  auto ThunkFn = [&](schir::Context& C, schir::ValueRefs) {
    Check_1 = IsAlive;
    // Continue normally.
    C.PushCont([&](schir::Context& C, schir::ValueRefs) {
      Check_2 = IsAlive;
      C.Cont();
    });
    C.Cont();
  };

  schir::ExternLambda<0, sizeof(ThunkFn)> Thunk;
  Thunk = ThunkFn;

  // Test the lifetime of managed objects
  // in a dynamic-wind environment.
  Context.DynamicWind(std::move(TrackerPtr), Thunk);
  EXPECT_TRUE(IsAlive);
  Context.Resume();
  EXPECT_TRUE(Check_1);
  EXPECT_TRUE(Check_2);
  EXPECT_FALSE(IsAlive);
}

TEST(ContextTest, DynamicWindExceptionExit) {
  auto Context = schir::Context();
  bool IsAlive = false;
  bool Check_1 = false;
  auto TrackerPtr = std::make_unique<LifetimeTracker>(IsAlive);

  auto ThunkFn = [&](schir::Context& C, schir::ValueRefs) {
    Check_1 = IsAlive;
    // Fail gloriously!
    C.Raise(schir::Undefined());
  };

  schir::ExternLambda<0, sizeof(ThunkFn)> Thunk;
  Thunk = ThunkFn;

  // Test the lifetime of managed objects
  // in a dynamic-wind environment.
  Context.DynamicWind(std::move(TrackerPtr), Thunk);
  EXPECT_TRUE(IsAlive);
  Context.Resume();
  EXPECT_TRUE(Check_1);
  EXPECT_FALSE(IsAlive);
}

}
