// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_SIMPLE_BUFFER_H_
#define QUICHE_BALSA_SIMPLE_BUFFER_H_

#include <cstddef>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

namespace test {
class SimpleBufferTest;
}  // namespace test

// SimpleBuffer stores data in a contiguous region.  It can grow on demand,
// which involves moving its data.  It keeps track of a read and a write
// position.  Reading consumes data.
class QUICHE_EXPORT SimpleBuffer {
 public:
  struct ReleasedBuffer {
    std::unique_ptr<char[]> buffer;
    size_t size;
  };

  SimpleBuffer() = default;
  // Create SimpleBuffer with at least `size` reserved capacity.
  explicit SimpleBuffer(int size);

  SimpleBuffer(const SimpleBuffer&) = delete;
  SimpleBuffer& operator=(const SimpleBuffer&) = delete;

  virtual ~SimpleBuffer() { delete[] storage_; }

  // Returns the number of bytes that can be read from the buffer.
  int ReadableBytes() const { return write_idx_ - read_idx_; }

  bool Empty() const { return read_idx_ == write_idx_; }

  // Copies `size` bytes to the buffer. Returns size.
  int Write(const char* bytes, int size);
  int WriteString(absl::string_view piece) {
    return Write(piece.data(), piece.size());
  }

  // Stores the pointer into the buffer that can be written to in `*ptr`, and
  // the number of characters that are allowed to be written in `*size`.  The
  // pointer and size can be used in functions like recv() or read().  If
  // `*size` is zero upon returning from this function, then it is unsafe to
  // dereference `*ptr`.  Writing to this region after calling any other
  // non-const method results in undefined behavior.
  void GetWritablePtr(char** ptr, int* size) const {
    *ptr = storage_ + write_idx_;
    *size = storage_size_ - write_idx_;
  }

  // Stores the pointer that can be read from in `*ptr`, and the number of bytes
  // that are allowed to be read in `*size`.  The pointer and size can be used
  // in functions like send() or write().  If `*size` is zero upon returning
  // from this function, then it is unsafe to dereference `*ptr`.  Reading from
  // this region after calling any other non-const method results in undefined
  // behavior.
  void GetReadablePtr(char** ptr, int* size) const {
    *ptr = storage_ + read_idx_;
    *size = write_idx_ - read_idx_;
  }

  // Returns the readable region as a string_view.  Reading from this region
  // after calling any other non-const method results in undefined behavior.
  absl::string_view GetReadableRegion() const {
    return absl::string_view(storage_ + read_idx_, write_idx_ - read_idx_);
  }

  // Reads bytes out of the buffer, and writes them into `bytes`.  Returns the
  // number of bytes read.  Consumes bytes from the buffer.
  int Read(char* bytes, int size);

  // Marks all data consumed, making the entire reserved buffer available for
  // write.  Does not resize or free up any memory.
  void Clear() { read_idx_ = write_idx_ = 0; }

  // Makes sure at least `size` bytes can be written into the buffer.  This can
  // be an expensive operation: costing a new and a delete, and copying of all
  // existing data. Even if the existing buffer does not need to be resized,
  // unread data may need to be moved to consolidate fragmented free space.
  void Reserve(int size);

  // Marks the oldest `amount_to_advance` bytes as consumed.
  // `amount_to_advance` must not be negative and it must not exceed
  // ReadableBytes().
  void AdvanceReadablePtr(int amount_to_advance);

  // Marks the first `amount_to_advance` bytes of the writable area written.
  // `amount_to_advance` must not be negative and it must not exceed the size of
  // the writable area, returned as the `size` outparam of GetWritablePtr().
  void AdvanceWritablePtr(int amount_to_advance);

  // Releases the current contents of the SimpleBuffer and returns them as a
  // unique_ptr<char[]>. Logically, has the same effect as calling Clear().
  ReleasedBuffer Release();

 private:
  friend class test::SimpleBufferTest;

  // The buffer owned by this class starts at `*storage_` and is `storage_size_`
  // bytes long.
  // If `storage_` is nullptr, then `storage_size_` must be zero.
  // `0 <= read_idx_ <= write_idx_ <= storage_size_` must always hold.
  // If `read_idx_ == write_idx_`, then they must be equal to zero.
  // The first `read_idx_` bytes of the buffer are consumed,
  // the next `write_idx_ - read_idx_` bytes are the readable region, and the
  // remaining `storage_size_ - write_idx_` bytes are the writable region.
  char* storage_ = nullptr;
  int write_idx_ = 0;
  int read_idx_ = 0;
  int storage_size_ = 0;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_SIMPLE_BUFFER_H_
