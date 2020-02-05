// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_pinnable_buffer_piece.h"

#include <new>

namespace spdy {

SpdyPinnableBufferPiece::SpdyPinnableBufferPiece()
    : buffer_(nullptr), length_(0) {}

SpdyPinnableBufferPiece::~SpdyPinnableBufferPiece() = default;

void SpdyPinnableBufferPiece::Pin() {
  if (!storage_ && buffer_ != nullptr && length_ != 0) {
    storage_.reset(new char[length_]);
    std::copy(buffer_, buffer_ + length_, storage_.get());
    buffer_ = storage_.get();
  }
}

void SpdyPinnableBufferPiece::Swap(SpdyPinnableBufferPiece* other) {
  size_t length = length_;
  length_ = other->length_;
  other->length_ = length;

  const char* buffer = buffer_;
  buffer_ = other->buffer_;
  other->buffer_ = buffer;

  storage_.swap(other->storage_);
}

}  // namespace spdy
