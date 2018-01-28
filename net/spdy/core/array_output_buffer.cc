// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/core/array_output_buffer.h"

namespace net {

void ArrayOutputBuffer::Next(char** data, int* size) {
  *data = current_;
  *size = capacity_ > 0 ? capacity_ : 0;
}

void ArrayOutputBuffer::AdvanceWritePtr(int64_t count) {
  current_ += count;
  capacity_ -= count;
}

uint64_t ArrayOutputBuffer::BytesFree() const {
  return capacity_;
}

}  // namespace net
