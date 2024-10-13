// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_SIMPLE_ARENA_H_
#define QUICHE_COMMON_QUICHE_SIMPLE_ARENA_H_

#include <memory>
#include <vector>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Allocates large blocks of memory, and doles them out in smaller chunks.
// Not thread-safe.
class QUICHE_EXPORT QuicheSimpleArena {
 public:
  class QUICHE_EXPORT Status {
   private:
    friend class QuicheSimpleArena;
    size_t bytes_allocated_;

   public:
    Status() : bytes_allocated_(0) {}
    size_t bytes_allocated() const { return bytes_allocated_; }
  };

  // Blocks allocated by this arena will be at least |block_size| bytes.
  explicit QuicheSimpleArena(size_t block_size);
  ~QuicheSimpleArena();

  // Copy and assign are not allowed.
  QuicheSimpleArena() = delete;
  QuicheSimpleArena(const QuicheSimpleArena&) = delete;
  QuicheSimpleArena& operator=(const QuicheSimpleArena&) = delete;

  // Move is allowed.
  QuicheSimpleArena(QuicheSimpleArena&& other);
  QuicheSimpleArena& operator=(QuicheSimpleArena&& other);

  char* Alloc(size_t size);
  char* Realloc(char* original, size_t oldsize, size_t newsize);
  char* Memdup(const char* data, size_t size);

  // If |data| and |size| describe the most recent allocation made from this
  // arena, the memory is reclaimed. Otherwise, this method is a no-op.
  void Free(char* data, size_t size);

  void Reset();

  Status status() const { return status_; }

 private:
  struct QUICHE_EXPORT Block {
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

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_SIMPLE_ARENA_H_
