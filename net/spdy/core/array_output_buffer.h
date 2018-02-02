// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_ARRAY_OUTPUT_BUFFER_H_
#define NET_SPDY_CORE_ARRAY_OUTPUT_BUFFER_H_

#include <cstddef>
#include "net/spdy/core/zero_copy_output_buffer.h"

namespace net {

class ArrayOutputBuffer : public ZeroCopyOutputBuffer {
 public:
  // |buffer| is pointed to the output to write to, and |size| is the capacity
  // of the output.
  ArrayOutputBuffer(char* buffer, int64_t size)
      : current_(buffer), begin_(buffer), capacity_(size) {}
  ~ArrayOutputBuffer() override {}

  ArrayOutputBuffer(const ArrayOutputBuffer&) = delete;
  ArrayOutputBuffer& operator=(const ArrayOutputBuffer&) = delete;

  void Next(char** data, int* size) override;
  void AdvanceWritePtr(int64_t count) override;
  uint64_t BytesFree() const override;

  size_t Size() const { return current_ - begin_; }
  char* Begin() const { return begin_; }

  // Resets the buffer to its original state.
  void Reset() {
    capacity_ += Size();
    current_ = begin_;
  }


 private:
  char* current_ = nullptr;
  char* begin_ = nullptr;
  int64_t capacity_ = 0;
};

}  // namespace net

#endif  // NET_SPDY_CORE_ARRAY_OUTPUT_BUFFER_H_
