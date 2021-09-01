// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_
#define QUICHE_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_

#include <stddef.h>

#include <memory>

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// Abstract base class for classes which allocate and delete buffers.
class QUIC_EXPORT_PRIVATE QuicBufferAllocator {
 public:
  virtual ~QuicBufferAllocator();

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
// QuicBufferAllocator through std::unique_ptr.
class QUIC_EXPORT_PRIVATE QuicBufferDeleter {
 public:
  explicit QuicBufferDeleter(QuicBufferAllocator* allocator)
      : allocator_(allocator) {}

  QuicBufferAllocator* allocator() { return allocator_; }
  void operator()(char* buffer) { allocator_->Delete(buffer); }

 private:
  QuicBufferAllocator* allocator_;
};

using QuicUniqueBufferPtr = std::unique_ptr<char[], QuicBufferDeleter>;

inline QuicUniqueBufferPtr MakeUniqueBuffer(QuicBufferAllocator* allocator,
                                            size_t size) {
  return QuicUniqueBufferPtr(allocator->New(size),
                             QuicBufferDeleter(allocator));
}

// QuicUniqueBufferPtr with a length attached to it.  Similar to QuicMemSlice,
// except unlike QuicMemSlice, QuicBuffer is mutable and is not
// platform-specific.  Also unlike QuicMemSlice, QuicBuffer can be empty.
class QUIC_EXPORT_PRIVATE QuicBuffer {
 public:
  QuicBuffer() : buffer_(nullptr, QuicBufferDeleter(nullptr)), size_(0) {}
  QuicBuffer(QuicBufferAllocator* allocator, size_t size)
      : buffer_(MakeUniqueBuffer(allocator, size)), size_(size) {}

  // Make sure the move constructor zeroes out the size field.
  QuicBuffer(QuicBuffer&& other)
      : buffer_(std::move(other.buffer_)), size_(other.size_) {
    other.buffer_ = nullptr;
    other.size_ = 0;
  }
  QuicBuffer& operator=(QuicBuffer&& other) {
    buffer_ = std::move(other.buffer_);
    size_ = other.size_;

    other.buffer_ = nullptr;
    other.size_ = 0;
    return *this;
  }

  // Convenience method to initialize a QuicBuffer by copying from an existing
  // one.
  static QuicBuffer Copy(QuicBufferAllocator* allocator,
                         absl::string_view data) {
    QuicBuffer result(allocator, data.size());
    memcpy(result.data(), data.data(), data.size());
    return result;
  }

  const char* data() const { return buffer_.get(); }
  char* data() { return buffer_.get(); }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  absl::string_view AsStringView() const {
    return absl::string_view(data(), size());
  }

  // Releases the ownership of the underlying buffer.
  QuicUniqueBufferPtr Release() {
    size_ = 0;
    return std::move(buffer_);
  }

 private:
  QuicUniqueBufferPtr buffer_;
  size_t size_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BUFFER_ALLOCATOR_H_
