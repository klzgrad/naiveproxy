// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_ZERO_COPY_OUTPUT_BUFFER_H_
#define NET_SPDY_CORE_ZERO_COPY_OUTPUT_BUFFER_H_

#include <cstdint>

namespace net {

class ZeroCopyOutputBuffer {
 public:
  virtual ~ZeroCopyOutputBuffer() {}

  // Returns the next available segment of memory to write. Will always return
  // the same segment until AdvanceWritePtr is called.
  virtual void Next(char** data, int* size) = 0;

  // After writing to a buffer returned from Next(), the caller should call
  // this method to indicate how many bytes were written.
  virtual void AdvanceWritePtr(int64_t count) = 0;

  // Returns the available capacity of the buffer.
  virtual uint64_t BytesFree() const = 0;
};

}  // namespace net

#endif  // NET_SPDY_CORE_ZERO_COPY_OUTPUT_BUFFER_H_
