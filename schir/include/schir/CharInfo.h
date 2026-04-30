//===----------------------------------------------------------------------===//
// Partially copied from llvm-project/clang/include/clang/Basic/CharInfo.h
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef SCHIR_CHARINFO_H
#define SCHIR_CHARINFO_H

#include <cstdint>

namespace schir::charinfo {
extern const uint16_t InfoTable[256];

enum {
  CHAR_HORZ_WS  = 0x0001,  // '\t', '\f', '\v'.  Note, no '\0'
  CHAR_VERT_WS  = 0x0002,  // '\r', '\n'
  CHAR_SPACE    = 0x0004,  // ' '
  CHAR_DIGIT    = 0x0008,  // 0-9
  CHAR_XLETTER  = 0x0010,  // a-f,A-F
  CHAR_UPPER    = 0x0020,  // A-Z
  CHAR_LOWER    = 0x0040,  // a-z
  // TODO Remove punctuation stuff. Replace with R7RS stuff.
  CHAR_UNDER    = 0x0080,  // _
  CHAR_PERIOD   = 0x0100,  // .
  CHAR_PUNCT    = 0x0200,  // {}[]#<>%:;?*+-/^&|~!=,"'`$@()
};

enum {
  CHAR_XUPPER = CHAR_XLETTER | CHAR_UPPER,
  CHAR_XLOWER = CHAR_XLETTER | CHAR_LOWER
};

inline
bool isWhitespace(unsigned char C) {
  return (InfoTable[C] & (CHAR_HORZ_WS|CHAR_VERT_WS|CHAR_SPACE)) != 0;
}

inline
bool isHorizontalWhitespace(unsigned char C) {
  return (InfoTable[C] & (CHAR_HORZ_WS|CHAR_SPACE)) != 0;
}

inline
bool isVerticalWhitespace(unsigned char C) {
  return (InfoTable[C] & CHAR_VERT_WS) != 0;
}
} // schir::charinfo

#endif // SCHIR_CHARINFO_H
