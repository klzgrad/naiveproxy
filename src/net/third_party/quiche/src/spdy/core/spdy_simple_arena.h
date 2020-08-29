// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_SIMPLE_ARENA_H_
#define QUICHE_SPDY_CORE_SPDY_SIMPLE_ARENA_H_

#include <memory>
#include <vector>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace spdy {

// Allocates large blocks of memory, and doles them out in smaller chunks.
// Not thread-safe.
class QUICHE_EXPORT_PRIVATE SpdySimpleArena {
 public:
  class Status {
   private:
    friend class SpdySimpleArena;
    size_t bytes_allocated_;

   public:
    Status() : bytes_allocated_(0) {}
    size_t bytes_allocated() const { return bytes_allocated_; }
  };

  // Blocks allocated by this arena will be at least |block_size| bytes.
  explicit SpdySimpleArena(size_t block_size);
  ~SpdySimpleArena();

  // Copy and assign are not allowed.
  SpdySimpleArena() = delete;
  SpdySimpleArena(const SpdySimpleArena&) = delete;
  SpdySimpleArena& operator=(const SpdySimpleArena&) = delete;

  // Move is allowed.
  SpdySimpleArena(SpdySimpleArena&& other);
  SpdySimpleArena& operator=(SpdySimpleArena&& other);

  char* Alloc(size_t size);
  char* Realloc(char* original, size_t oldsize, size_t newsize);
  char* Memdup(const char* data, size_t size);

  // If |data| and |size| describe the most recent allocation made from this
  // arena, the memory is reclaimed. Otherwise, this method is a no-op.
  void Free(char* data, size_t size);

  void Reset();

  Status status() const { return status_; }

 private:
  struct Block {
    std::unique_ptr<char[]> data;
    size_t size = 0;
    size_t used = 0;

    explicit Block(size_t s);
    ~Block();

    Block(Block&& other);
    Block& operator=(Block&& other);
  };

  void Reserve(size_t additional_space);
  void AllocBlock(size_t size);

  size_t block_size_;
  std::vector<Block> blocks_;
  Status status_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_SIMPLE_ARENA_H_
