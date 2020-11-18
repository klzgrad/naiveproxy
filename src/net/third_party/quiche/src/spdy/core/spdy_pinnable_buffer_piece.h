// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_PINNABLE_BUFFER_PIECE_H_
#define QUICHE_SPDY_CORE_SPDY_PINNABLE_BUFFER_PIECE_H_

#include <stddef.h>

#include <memory>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace spdy {

class SpdyPrefixedBufferReader;

// Helper class of SpdyPrefixedBufferReader.
// Represents a piece of consumed buffer which may (or may not) own its
// underlying storage. Users may "pin" the buffer at a later time to ensure
// a SpdyPinnableBufferPiece owns and retains storage of the buffer.
struct QUICHE_EXPORT_PRIVATE SpdyPinnableBufferPiece {
 public:
  SpdyPinnableBufferPiece();
  ~SpdyPinnableBufferPiece();

  const char* buffer() const { return buffer_; }

  explicit operator quiche::QuicheStringPiece() const {
    return quiche::QuicheStringPiece(buffer_, length_);
  }

  // Allocates and copies the buffer to internal storage.
  void Pin();

  bool IsPinned() const { return storage_ != nullptr; }

  // Swaps buffers, including internal storage, with |other|.
  void Swap(SpdyPinnableBufferPiece* other);

 private:
  friend class SpdyPrefixedBufferReader;

  const char* buffer_;
  size_t length_;
  // Null iff |buffer_| isn't pinned.
  std::unique_ptr<char[]> storage_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_PINNABLE_BUFFER_PIECE_H_
