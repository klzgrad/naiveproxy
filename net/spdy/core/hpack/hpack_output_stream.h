// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_HPACK_HPACK_OUTPUT_STREAM_H_
#define NET_SPDY_CORE_HPACK_HPACK_OUTPUT_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#include <map>

#include "base/macros.h"
#include "net/spdy/core/hpack/hpack_constants.h"
#include "net/spdy/platform/api/spdy_export.h"
#include "net/spdy/platform/api/spdy_string.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

// All section references below are to
// http://tools.ietf.org/html/draft-ietf-httpbis-header-compression-08

namespace net {

// An HpackOutputStream handles all the low-level details of encoding
// header fields.
class SPDY_EXPORT_PRIVATE HpackOutputStream {
 public:
  HpackOutputStream();
  ~HpackOutputStream();

  // Appends the lower |bit_size| bits of |bits| to the internal buffer.
  //
  // |bit_size| must be > 0 and <= 8. |bits| must not have any bits
  // set other than the lower |bit_size| bits.
  void AppendBits(uint8_t bits, size_t bit_size);

  // Simply forwards to AppendBits(prefix.bits, prefix.bit-size).
  void AppendPrefix(HpackPrefix prefix);

  // Directly appends |buffer|.
  void AppendBytes(SpdyStringPiece buffer);

  // Appends the given integer using the representation described in
  // 6.1. If the internal buffer ends on a byte boundary, the prefix
  // length N is taken to be 8; otherwise, it is taken to be the
  // number of bits to the next byte boundary.
  //
  // It is guaranteed that the internal buffer will end on a byte
  // boundary after this function is called.
  void AppendUint32(uint32_t I);

  // Swaps the internal buffer with |output|, then resets state.
  void TakeString(SpdyString* output);

  // Gives up to |max_size| bytes of the internal buffer to |output|. Resets
  // internal state with the overflow.
  void BoundedTakeString(size_t max_size, SpdyString* output);

  // Size in bytes of stream's internal buffer.
  size_t size() const { return buffer_.size(); }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  // The internal bit buffer.
  SpdyString buffer_;

  // If 0, the buffer ends on a byte boundary. If non-zero, the buffer
  // ends on the nth most significant bit. Guaranteed to be < 8.
  size_t bit_offset_;

  DISALLOW_COPY_AND_ASSIGN(HpackOutputStream);
};

}  // namespace net

#endif  // NET_SPDY_CORE_HPACK_HPACK_OUTPUT_STREAM_H_
