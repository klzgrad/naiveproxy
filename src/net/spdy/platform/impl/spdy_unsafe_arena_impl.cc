// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/platform/impl/spdy_unsafe_arena_impl.h"

#include <string.h>

#include <algorithm>

#include "base/logging.h"

namespace spdy {

SpdyUnsafeArenaImpl::SpdyUnsafeArenaImpl(size_t block_size)
    : block_size_(block_size) {}

SpdyUnsafeArenaImpl::~SpdyUnsafeArenaImpl() = default;

SpdyUnsafeArenaImpl::SpdyUnsafeArenaImpl(SpdyUnsafeArenaImpl&& other) = default;
SpdyUnsafeArenaImpl& SpdyUnsafeArenaImpl::operator=(
    SpdyUnsafeArenaImpl&& other) = default;

char* SpdyUnsafeArenaImpl::Alloc(size_t size) {
  Reserve(size);
  Block& b = blocks_.back();
  DCHECK_GE(b.size, b.used + size);
  char* out = b.data.get() + b.used;
  b.used += size;
  return out;
}

char* SpdyUnsafeArenaImpl::Realloc(char* original,
                                   size_t oldsize,
                                   size_t newsize) {
  DCHECK(!blocks_.empty());
  Block& last = blocks_.back();
  if (last.data.get() <= original && original < last.data.get() + last.size) {
    // (original, oldsize) is in the last Block.
    DCHECK_GE(last.data.get() + last.used, original + oldsize);
    if (original + oldsize == last.data.get() + last.used) {
      // (original, oldsize) was the most recent allocation,
      if (original + newsize < last.data.get() + last.size) {
        // (original, newsize) fits in the same Block.
        last.used += newsize - oldsize;
        return original;
      }
    }
  }
  char* out = Alloc(newsize);
  memcpy(out, original, oldsize);
  return out;
}

char* SpdyUnsafeArenaImpl::Memdup(const char* data, size_t size) {
  char* out = Alloc(size);
  memcpy(out, data, size);
  return out;
}

void SpdyUnsafeArenaImpl::Free(char* data, size_t size) {
  if (blocks_.empty()) {
    return;
  }
  Block& b = blocks_.back();
  if (size <= b.used && data + size == b.data.get() + b.used) {
    // The memory region passed by the caller was the most recent allocation
    // from the final block in this arena.
    b.used -= size;
  }
}

void SpdyUnsafeArenaImpl::Reset() {
  blocks_.clear();
  status_.bytes_allocated_ = 0;
}

void SpdyUnsafeArenaImpl::Reserve(size_t additional_space) {
  if (blocks_.empty()) {
    AllocBlock(std::max(additional_space, block_size_));
  } else {
    const Block& last = blocks_.back();
    if (last.size < last.used + additional_space) {
      AllocBlock(std::max(additional_space, block_size_));
    }
  }
}

void SpdyUnsafeArenaImpl::AllocBlock(size_t size) {
  blocks_.push_back(Block(size));
  status_.bytes_allocated_ += size;
}

SpdyUnsafeArenaImpl::Block::Block(size_t s)
    : data(new char[s]), size(s), used(0) {}

SpdyUnsafeArenaImpl::Block::~Block() = default;

SpdyUnsafeArenaImpl::Block::Block(SpdyUnsafeArenaImpl::Block&& other)
    : size(other.size), used(other.used) {
  data = std::move(other.data);
}

SpdyUnsafeArenaImpl::Block& SpdyUnsafeArenaImpl::Block::operator=(
    SpdyUnsafeArenaImpl::Block&& other) {
  size = other.size;
  used = other.used;
  data = std::move(other.data);
  return *this;
}

}  // namespace spdy
