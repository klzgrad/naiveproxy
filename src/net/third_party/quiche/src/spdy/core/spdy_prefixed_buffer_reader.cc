// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_prefixed_buffer_reader.h"

#include <new>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {

SpdyPrefixedBufferReader::SpdyPrefixedBufferReader(const char* prefix,
                                                   size_t prefix_length,
                                                   const char* suffix,
                                                   size_t suffix_length)
    : prefix_(prefix),
      suffix_(suffix),
      prefix_length_(prefix_length),
      suffix_length_(suffix_length) {}

size_t SpdyPrefixedBufferReader::Available() {
  return prefix_length_ + suffix_length_;
}

bool SpdyPrefixedBufferReader::ReadN(size_t count, char* out) {
  if (Available() < count) {
    return false;
  }

  if (prefix_length_ >= count) {
    // Read is fully satisfied by the prefix.
    std::copy(prefix_, prefix_ + count, out);
    prefix_ += count;
    prefix_length_ -= count;
    return true;
  } else if (prefix_length_ != 0) {
    // Read is partially satisfied by the prefix.
    out = std::copy(prefix_, prefix_ + prefix_length_, out);
    count -= prefix_length_;
    prefix_length_ = 0;
    // Fallthrough to suffix read.
  }
  DCHECK(suffix_length_ >= count);
  // Read is satisfied by the suffix.
  std::copy(suffix_, suffix_ + count, out);
  suffix_ += count;
  suffix_length_ -= count;
  return true;
}

bool SpdyPrefixedBufferReader::ReadN(size_t count,
                                     SpdyPinnableBufferPiece* out) {
  if (Available() < count) {
    return false;
  }

  out->storage_.reset();
  out->length_ = count;

  if (prefix_length_ >= count) {
    // Read is fully satisfied by the prefix.
    out->buffer_ = prefix_;
    prefix_ += count;
    prefix_length_ -= count;
    return true;
  } else if (prefix_length_ != 0) {
    // Read is only partially satisfied by the prefix. We need to allocate
    // contiguous storage as the read spans the prefix & suffix.
    out->storage_.reset(new char[count]);
    out->buffer_ = out->storage_.get();
    ReadN(count, out->storage_.get());
    return true;
  } else {
    DCHECK(suffix_length_ >= count);
    // Read is fully satisfied by the suffix.
    out->buffer_ = suffix_;
    suffix_ += count;
    suffix_length_ -= count;
    return true;
  }
}

}  // namespace spdy
