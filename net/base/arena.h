// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_ARENA_H_
#define NET_BASE_ARENA_H_

#include <memory>
#include <vector>

#include "net/base/net_export.h"

namespace net {

// Allocates large blocks of memory, and doles them out in smaller chunks.
// Not thread-safe.
class NET_EXPORT_PRIVATE UnsafeArena {
 public:
  class Status {
   private:
    friend class UnsafeArena;
    size_t bytes_allocated_;

   public:
    Status() : bytes_allocated_(0) {}
    size_t bytes_allocated() const { return bytes_allocated_; }
  };

  // Blocks allocated by this arena will be at least |block_size| bytes.
  explicit UnsafeArena(size_t block_size);
  ~UnsafeArena();

  // Copy and assign are not allowed.
  UnsafeArena() = delete;
  UnsafeArena(const UnsafeArena&) = delete;
  UnsafeArena& operator=(const UnsafeArena&) = delete;

  // Move is allowed.
  UnsafeArena(UnsafeArena&& other);
  UnsafeArena& operator=(UnsafeArena&& other);

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

}  // namespace net

#endif  // NET_BASE_ARENA_H_
