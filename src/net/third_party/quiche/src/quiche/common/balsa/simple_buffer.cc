// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/balsa/simple_buffer.h"

#include <cstring>
#include <memory>

#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

static const int kInitialSimpleBufferSize = 10;

SimpleBuffer::SimpleBuffer()
    : storage_(new char[kInitialSimpleBufferSize]),
      write_idx_(0),
      read_idx_(0),
      storage_size_(kInitialSimpleBufferSize) {}

SimpleBuffer::SimpleBuffer(int size)
    : write_idx_(0), read_idx_(0), storage_size_(size) {
  // Callers may try to allocate overly large blocks, but negative sizes are
  // obviously wrong.
  QUICHE_CHECK_GE(size, 0);
  storage_ = new char[size];
}

////////////////////////////////////////////////////////////////////////////////

int SimpleBuffer::Write(const char* bytes, int size) {
  if (size < 0) {
    QUICHE_BUG(simple_buffer_write_negative_size)
        << "size must not be negative: " << size;
    return 0;
  }

  bool has_room = ((storage_size_ - write_idx_) >= size);
  if (!has_room) {
    Reserve(size);
  }
  memcpy(storage_ + write_idx_, bytes, size);
  AdvanceWritablePtr(size);
  return size;
}

////////////////////////////////////////////////////////////////////////////////

int SimpleBuffer::Read(char* bytes, int size) {
  if (size < 0) {
    QUICHE_BUG(simple_buffer_read_negative_size)
        << "size must not be negative: " << size;
    return 0;
  }

  char* read_ptr = nullptr;
  int read_size = 0;
  GetReadablePtr(&read_ptr, &read_size);
  if (read_size > size) {
    read_size = size;
  }
  memcpy(bytes, read_ptr, read_size);
  AdvanceReadablePtr(read_size);
  return read_size;
}

////////////////////////////////////////////////////////////////////////////////

// Attempts to reserve a contiguous block of buffer space either by reclaiming
// consumed data or by allocating a larger buffer.
void SimpleBuffer::Reserve(int size) {
  if (size < 0) {
    QUICHE_BUG(simple_buffer_reserve_negative_size)
        << "size must not be negative: " << size;
    return;
  }

  if (size == 0 || storage_size_ - write_idx_ >= size) {
    return;
  }

  char* read_ptr = nullptr;
  int read_size = 0;
  GetReadablePtr(&read_ptr, &read_size);

  if (read_size + size <= storage_size_) {
    // Can reclaim space from consumed bytes by shifting.
    memmove(storage_, read_ptr, read_size);
    read_idx_ = 0;
    write_idx_ = read_size;
    return;
  }

  // The new buffer needs to be at least `read_size + size` bytes.
  // At least double the buffer to amortize allocation costs.
  int new_storage_size = 2 * storage_size_;
  if (new_storage_size < size + read_size) {
    new_storage_size = size + read_size;
  }

  char* new_storage = new char[new_storage_size];
  memcpy(new_storage, read_ptr, read_size);
  delete[] storage_;

  read_idx_ = 0;
  write_idx_ = read_size;
  storage_ = new_storage;
  storage_size_ = new_storage_size;
}

void SimpleBuffer::AdvanceReadablePtr(int amount_to_advance) {
  if (amount_to_advance < 0) {
    QUICHE_BUG(simple_buffer_advance_read_negative_arg)
        << "amount_to_advance must not be negative: " << amount_to_advance;
    return;
  }

  read_idx_ += amount_to_advance;
  if (read_idx_ > write_idx_) {
    QUICHE_BUG(simple_buffer_read_ptr_too_far)
        << "error: readable pointer advanced beyond writable one";
    read_idx_ = write_idx_;
  }

  if (read_idx_ == write_idx_) {
    // Buffer is empty, rewind `read_idx_` and `write_idx_` so that next write
    // happens at the beginning of buffer instead of cutting free space in two.
    Clear();
  }
}

void SimpleBuffer::AdvanceWritablePtr(int amount_to_advance) {
  if (amount_to_advance < 0) {
    QUICHE_BUG(simple_buffer_advance_write_negative_arg)
        << "amount_to_advance must not be negative: " << amount_to_advance;
    return;
  }

  write_idx_ += amount_to_advance;
  if (write_idx_ > storage_size_) {
    QUICHE_BUG(simple_buffer_write_ptr_too_far)
        << "error: writable pointer advanced beyond end of storage";
    write_idx_ = storage_size_;
  }
}

QuicheMemSlice SimpleBuffer::ReleaseAsSlice() {
  if (write_idx_ == 0) {
    return QuicheMemSlice();
  }
  QuicheMemSlice slice(std::unique_ptr<char[]>(storage_), write_idx_);
  Clear();
  storage_ = nullptr;
  storage_size_ = 0;
  return slice;
}
}  // namespace quiche
