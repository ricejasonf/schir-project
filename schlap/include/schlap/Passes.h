#ifndef SCHLAP_PASSES_H
#define SCHLAP_PASSES_H

#include <schlap/Dialect.h>
#include <mlir/Pass/Pass.h>

namespace schlap {
#define GEN_PASS_DECL
#include "schlap/SchlapPasses.h.inc"

#define GEN_PASS_REGISTRATION
#include "schlap/SchlapPasses.h.inc"
}  // namespace schlap

#endif  // SCHLAP_PASSES_H
