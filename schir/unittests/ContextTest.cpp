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

class LifetimeTracker {
  bool& IsAlive;

public:
  explicit LifetimeTracker(bool& IsAlive_)
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

TEST(ContextTest, DynamicWindExceptionExitAfterInner) {
  auto Context = schir::Context();
  bool IsAlive = false;
  bool Check_1 = false;
  auto TrackerPtr = std::make_unique<LifetimeTracker>(IsAlive);

  schir::Value Noop = Context.CreateLambda(
    [](schir::Context& C, schir::ValueRefs) { C.Cont(); });
  schir::Value InnerAfter = Context.CreateLambda(
    [&](schir::Context& C, schir::ValueRefs) {
      Check_1 = IsAlive;
      C.Cont();
    });
  schir::Value Fail = Context.CreateLambda(
    [](schir::Context& C, schir::ValueRefs) {
      C.Raise(schir::Undefined());
    });
  schir::Value Thunk = Context.CreateLambda(
    [](schir::Context& C, schir::ValueRefs) {
      C.DynamicWind(C.getCapture(0), C.getCapture(1), C.getCapture(2));
    }, schir::CaptureList{Noop, Fail, InnerAfter});

  // Destroy the managed object after the inner after-thunk on error.
  Context.DynamicWind(std::move(TrackerPtr), Thunk);
  Context.Resume();
  EXPECT_TRUE(Check_1);
  EXPECT_FALSE(IsAlive);
}

TEST(ContextTest, DynamicWindReenterDestroyed) {
  auto Context = schir::Context();
  bool IsAlive = false;
  int BeforeCount = 0;
  int ResumeCount = 0;
  schir::Value EscapeProc;
  auto TrackerPtr = std::make_unique<LifetimeTracker>(IsAlive);

  schir::Value Before = Context.CreateLambda(
    [&](schir::Context& C, schir::ValueRefs) {
      ++BeforeCount;
      C.Cont();
    });
  schir::Value After = Context.CreateLambda(
    [](schir::Context& C, schir::ValueRefs) { C.Cont(); });
  schir::Value Thunk = Context.CreateLambda(
    [&](schir::Context& C, schir::ValueRefs) {
      C.PushCont([&](schir::Context& C, schir::ValueRefs) {
        ++ResumeCount;
        C.Cont();
      });
      // Save an escape procedure into the extent.
      C.CallCC(C.CreateLambda([&](schir::Context& C, schir::ValueRefs Args) {
        EscapeProc = Args[0];
        C.Cont();
      }));
    });

  Context.DynamicWind(std::move(TrackerPtr), Before, Thunk, After);
  Context.Resume();
  EXPECT_EQ(BeforeCount, 1);
  EXPECT_EQ(ResumeCount, 1);
  EXPECT_FALSE(IsAlive);

  // Do not reenter the extent of the destroyed object.
  Context.Apply(EscapeProc, schir::Value(schir::Undefined()));
  Context.Resume();
  EXPECT_EQ(BeforeCount, 1);
  EXPECT_EQ(ResumeCount, 1);
  EXPECT_FALSE(IsAlive);
}

TEST(ContextTest, SharedAnyLifetime) {
  auto Context = schir::Context();
  bool IsAlive = false;
  bool Check_1 = false;
  bool Check_2 = false;

  schir::Value LTTV = Context.CreateSharedAny(
      std::make_shared<LifetimeTracker>(IsAlive));

  schir::Lambda* Thunk = Context.CreateLambda(
    [&](schir::Context& C, schir::ValueRefs) {
      LifetimeTracker* LTT = schir::any_cast<LifetimeTracker>(C.getCapture(0));
      Check_1 = IsAlive;
      Check_2 = LTT;
      C.Cont();
    }, schir::CaptureList{LTTV});

  schir::ContextLocal GlobalVar;
  GlobalVar.set(Context, Thunk);

  // TODO Run GC.

  Context.ApplyThunk(Thunk);
  EXPECT_TRUE(IsAlive);
  Context.Resume();
  EXPECT_TRUE(Check_1);
  EXPECT_TRUE(Check_2);
  EXPECT_TRUE(IsAlive);

  GlobalVar.set(Context, schir::Bool(false));
  // TODO Run GC.
  //EXPECT_FALSE(IsAlive);
}
}
