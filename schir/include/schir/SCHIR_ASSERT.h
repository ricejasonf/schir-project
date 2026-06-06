#ifndef SCHIR_ASSERT_H
#define SCHIR_ASSERT_H

#include <cstdio>
#include <cstdlib>

#define SCHIR_ASSERT(SCHIR_ASSERT_COND) \
{if (!(SCHIR_ASSERT_COND)) { \
  std::fprintf(stderr, "Assertion\n\t\"%s\"\nfailed at %s:%d\n", \
    #SCHIR_ASSERT_COND, __FILE__, __LINE__); \
  std::exit(1); \
}}

#endif // SCHIR_ASSERT_H
