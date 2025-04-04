// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HPACK_OUTPUT_STREAM_H_
#define QUICHE_HTTP2_HPACK_HPACK_OUTPUT_STREAM_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/http2/hpack/hpack_constants.h"
#include "quiche/common/platform/api/quiche_export.h"

// All section references below are to
// http://tools.ietf.org/html/draft-ietf-httpbis-header-compression-08

namespace spdy {

// An HpackOutputStream handles all the low-level details of encoding
// header fields.
class QUICHE_EXPORT HpackOutputStream {
 public:
  HpackOutputStream();
  HpackOutputStream(const HpackOutputStream&) = delete;
  HpackOutputStream& operator=(const HpackOutputStream&) = delete;
  ~HpackOutputStream();

  // Appends the lower |bit_size| bits of |bits| to the internal buffer.
  //
  // |bit_size| must be > 0 and <= 8. |bits| must not have any bits
  // set other than the lower |bit_size| bits.
  void AppendBits(uint8_t bits, size_t bit_size);

  // Simply forwards to AppendBits(prefix.bits, prefix.bit-size).
  void AppendPrefix(HpackPrefix prefix);

  // Directly appends |buffer|.
  void AppendBytes(absl::string_view buffer);

  // Appends the given integer using the representation described in
  // 6.1. If the internal buffer ends on a byte boundary, the prefix
  // length N is taken to be 8; otherwise, it is taken to be the
  // number of bits to the next byte boundary.
  //
  // It is guaranteed that the internal buffer will end on a byte
  // boundary after this function is called.
  void AppendUint32(uint32_t I);

  // Return pointer to internal buffer.  |bit_offset_| needs to be zero.
  std::string* MutableString();

  // Returns the internal buffer as a string, then resets state.
  std::string TakeString();

  // Returns up to |max_size| bytes of the internal buffer. Resets
  // internal state with the overflow.
  std::string BoundedTakeString(size_t max_size);

  // Size in bytes of stream's internal buffer.
  size_t size() const { return buffer_.size(); }

 private:
  // The internal bit buffer.
  std::string buffer_;

  // If 0, the buffer ends on a byte boundary. If non-zero, the buffer
  // ends on the nth most significant bit. Guaranteed to be < 8.
  size_t bit_offset_;
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_HPACK_HPACK_OUTPUT_STREAM_H_
