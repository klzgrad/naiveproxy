// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_
#define NET_THIRD_PARTY_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_

#include <cstdint>

#include "net/third_party/http2/platform/api/http2_export.h"
#include "net/third_party/http2/platform/api/http2_string.h"

namespace http2 {

// HPACK integer encoder class implementing variable length integer
// representation defined in RFC7541, Section 5.1:
// https://httpwg.org/specs/rfc7541.html#integer.representation
class HTTP2_EXPORT_PRIVATE HpackVarintEncoder {
 public:
  HpackVarintEncoder();

  // Start encoding an integer.  Return the first encoded byte (composed of
  // optional high bits and 1 to 8 bit long prefix).  It is possible that this
  // completes the encoding.  Must not be called when previously started
  // encoding is still in progress.
  unsigned char StartEncoding(uint8_t high_bits,
                              uint8_t prefix_length,
                              uint64_t varint);

  // Continue encoding the integer |varint| passed in to StartEncoding().
  // Append the next at most |max_encoded_bytes| encoded octets to |output|.
  // Returns the number of encoded octets.  Must not be called unless a
  // previously started encoding is still in progress.
  size_t ResumeEncoding(size_t max_encoded_bytes, Http2String* output);

  // Returns true if encoding an integer has started and is not completed yet.
  bool IsEncodingInProgress() const;

 private:
  // The original integer shifted to the right by the number of bits already
  // encoded.  The lower bits shifted away have already been encoded, and
  // |varint_| has the higher bits that remain to be encoded.
  uint64_t varint_;

  // True when encoding an integer has started and is not completed yet.
  bool encoding_in_progress_;
};

}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_HPACK_VARINT_HPACK_VARINT_ENCODER_H_
