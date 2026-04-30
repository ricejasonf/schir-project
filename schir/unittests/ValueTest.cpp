//=== - ValueTest.cpp - Tests for schir::Value and associated member types --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------------===//

#include "schir/Value.h"

#include <gtest/gtest.h>

namespace schir {

TEST(ValueTest, NullTest) {
  EXPECT_FALSE(bool(schir::Value(nullptr)));

  {
    schir::ValueBase* PtrNull = nullptr;
    EXPECT_FALSE(bool(schir::Value(PtrNull)));
  }
  {
    schir::Operation* PtrNull = nullptr;
    EXPECT_FALSE(bool(schir::Value(PtrNull)));
  }
}

TEST(ValueTest, IntTest) {
  // Value should be "true" if it is not a nullptr
  EXPECT_TRUE(bool(schir::Value(schir::Int{0})))
    << "value should be true for non-nullptr";

  schir::Value V = schir::Int{42}; 
  ASSERT_TRUE(llvm::isa<schir::Int>(V));
  schir::Int I = llvm::cast<schir::Int>(V);
  EXPECT_EQ(I, schir::Int{42});
  EXPECT_NE(schir::Int{12}, schir::Int{0});
}

TEST(ValueTest, BoolTest) {
  // Value should be "true" if it is not a nullptr
  EXPECT_TRUE(bool(schir::Value(schir::Bool{false})))
    << "value should be true for non-nullptr";

  EXPECT_FALSE(!schir::Value(schir::Bool{false}))
    << "value should be true for non-nullptr";

  EXPECT_TRUE(schir::Bool{true});
  EXPECT_FALSE(schir::Bool{false});

  schir::Value V = schir::Bool{true}; 
  ASSERT_TRUE(llvm::isa<schir::Bool>(V));
  schir::Bool B = llvm::cast<schir::Bool>(V);
  EXPECT_EQ(B, schir::Bool{true});

  // isTrue is for Scheme conditional expressions
  V = nullptr;
  EXPECT_TRUE(V.isTrue());
  V = schir::Bool{true};
  EXPECT_TRUE(V.isTrue());
  V = schir::Int{0};
  EXPECT_TRUE(V.isTrue());
  V = schir::Bool{false};
  EXPECT_FALSE(V.isTrue());

}

}
