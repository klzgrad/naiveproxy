// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_BUFFER_ALLOCATOR_H_
#define QUICHE_COMMON_QUICHE_BUFFER_ALLOCATOR_H_

#include <stddef.h>

#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_iovec.h"

namespace quiche {

// Abstract base class for classes which allocate and delete buffers.
class QUICHE_EXPORT QuicheBufferAllocator {
 public:
  virtual ~QuicheBufferAllocator() = default;

  // Returns or allocates a new buffer of |size|. Never returns null.
  virtual char* New(size_t size) = 0;

  // Returns or allocates a new buffer of |size| if |flag_enable| is true.
  // Otherwise, returns a buffer that is compatible with this class directly
  // with operator new. Never returns null.
  virtual char* New(size_t size, bool flag_enable) = 0;

  // Releases a buffer.
  virtual void Delete(char* buffer) = 0;

  // Marks the allocator as being idle. Serves as a hint to notify the allocator
  // that it should release any resources it's still holding on to.
  virtual void MarkAllocatorIdle() {}
};

// A deleter that can be used to manage ownership of buffers allocated via
// QuicheBufferAllocator through std::unique_ptr.
class QUICHE_EXPORT QuicheBufferDeleter {
 public:
  explicit QuicheBufferDeleter(QuicheBufferAllocator* allocator)
      : allocator_(allocator) {}

  QuicheBufferAllocator* allocator() { return allocator_; }
  void operator()(char* buffer) { allocator_->Delete(buffer); }

 private:
  QuicheBufferAllocator* allocator_;
};

using QuicheUniqueBufferPtr = std::unique_ptr<char[], QuicheBufferDeleter>;

inline QuicheUniqueBufferPtr MakeUniqueBuffer(QuicheBufferAllocator* allocator,
                                              size_t size) {
  return QuicheUniqueBufferPtr(allocator->New(size),
                               QuicheBufferDeleter(allocator));
}

// QuicheUniqueBufferPtr with a length attached to it.  Similar to
// QuicheMemSlice, except unlike QuicheMemSlice, QuicheBuffer is mutable and is
// not platform-specific.  Also unlike QuicheMemSlice, QuicheBuffer can be
// empty.
class QUICHE_EXPORT QuicheBuffer {
 public:
  QuicheBuffer() : buffer_(nullptr, QuicheBufferDeleter(nullptr)), size_(0) {}
  QuicheBuffer(QuicheBufferAllocator* allocator, size_t size)
      : buffer_(MakeUniqueBuffer(allocator, size)), size_(size) {}

  QuicheBuffer(QuicheUniqueBufferPtr buffer, size_t size)
      : buffer_(std::move(buffer)), size_(size) {}

  // Make sure the move constructor zeroes out the size field.
  QuicheBuffer(QuicheBuffer&& other)
      : buffer_(std::move(other.buffer_)), size_(other.size_) {
    other.buffer_ = nullptr;
    other.size_ = 0;
  }
  QuicheBuffer& operator=(QuicheBuffer&& other) {
    buffer_ = std::move(other.buffer_);
    size_ = other.size_;

    other.buffer_ = nullptr;
    other.size_ = 0;
    return *this;
  }

  // Factory method to create a QuicheBuffer that holds a copy of `data`.
  static QuicheBuffer Copy(QuicheBufferAllocator* allocator,
                           absl::string_view data) {
    QuicheBuffer buffer(allocator, data.size());
    memcpy(buffer.data(), data.data(), data.size());
    return buffer;
  }

  // Factory method to create a QuicheBuffer of length `buffer_length` that
  // holds a copy of `buffer_length` bytes from `iov` starting at offset
  // `iov_offset`.  `iov` must be at least `iov_offset + buffer_length` total
  // length.
  static QuicheBuffer CopyFromIovec(QuicheBufferAllocator* allocator,
                                    const struct iovec* iov, int iov_count,
                                    size_t iov_offset, size_t buffer_length);

  const char* data() const { return buffer_.get(); }
  char* data() { return buffer_.get(); }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  absl::string_view AsStringView() const {
    return absl::string_view(data(), size());
  }

  // Releases the ownership of the underlying buffer.
  QuicheUniqueBufferPtr Release() {
    size_ = 0;
    return std::move(buffer_);
  }

 private:
  QuicheUniqueBufferPtr buffer_;
  size_t size_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_BUFFER_ALLOCATOR_H_
