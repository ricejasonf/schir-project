//===--- Heap.h - Classes for representing declarations ----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares schir::Heap.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SCHIR_HEAP_H
#define LLVM_SCHIR_HEAP_H

#include "schir/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Allocator.h"
#include <algorithm>

namespace schir {
template <typename Derived>
class Heap;
}

template <typename Derived>
void* operator new(size_t Size, schir::Heap<Derived>& Heap);
template <typename Derived>
void operator delete(void*, schir::Heap<Derived>&) { }


namespace schir {
using llvm::ArrayRef;
using llvm::StringRef;
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;

// Implement CopyCollector in Heap.cpp.
class CopyCollector;

// Derived implements CollectGarbage() which calls
// VisitRootGC(Value) for each root node.
template <typename Derived>
class Heap : public llvm::AllocatorBase<Heap<Derived>> {
  friend Derived;
  using AllocatorTy = llvm::BumpPtrAllocator;

  Derived& getDerived() {
    return *static_cast<Derived*>(this);
  }

  AllocatorTy TrashHeap;
  size_t BytesUsed = 0;

  // MaxHint - The threshold used to determine if a garbage
  //           collection run is needed. This value is not a
  //           hard limit and the limit is increased when a
  //           collection run yields a low return. The limit
  //           may also increase with the allocation of a large
  //           object.
  size_t MaxHint;

public:
  Heap(size_t MaxStart)
    : MaxHint(MaxStart)
  { }

  // Return the number bytes used. This will not include
  // bytes used by alignment padding or trailing red zones.
  size_t getBytesUsed() const {
    return BytesUsed;
  }

  // Allocate a potentially large object (like a String).
  void* BigAllocate(size_t Size, size_t Alignment) {
    size_t WorstCase = Size + Alignment;
    if (WorstCase >=
          Derived::MiB && double(WorstCase) / double(BytesUsed) > 0.25) {
      // Just increase the MaxHint since GC will not
      // likely do much in this scenario.
      MaxHint += WorstCase;
    }

    return Allocate(Size, Alignment);
  }

  void* Allocate(size_t Size, size_t Alignment) {
    BytesUsed += Size;
    return TrashHeap.Allocate(Size, Alignment);
  }

  void Deallocate(void const*, size_t, size_t) {
    // Do nothing.
  }

  void MaybeCollectGarbage() {
    if (BytesUsed > MaxHint)
       getDerived().CollectGarbage();
  }

  void ReplaceHeap(AllocatorTy&& Alloc) {
    TrashHeap = std::move(Alloc);
  }
};

class IdTable {
  using AllocatorTy = llvm::BumpPtrAllocator;

  // Provide a special heap for identifiers
  // so their pointers are stable to be used
  // as keys in maps like Module, Context, etc.
  AllocatorTy IdHeap;
  llvm::StringMap<String*> IdTableMap = {};
  std::string Buffer = {};

public:
  String* CreateIdTableEntry(llvm::StringRef S);
  String* CreateIdTableEntry(llvm::StringRef Prefix, llvm::StringRef S);
};

}  // namespace schir

template <typename Derived>
void* operator new(size_t Size, schir::Heap<Derived>& Heap) {
  return Heap.Allocate(Size,
    std::min((size_t)llvm::NextPowerOf2(Size), alignof(std::max_align_t)));
}

#endif
