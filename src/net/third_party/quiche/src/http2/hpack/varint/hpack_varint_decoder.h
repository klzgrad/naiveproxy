// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// HpackVarintDecoder decodes HPACK variable length unsigned integers. In HPACK,
// these integers are used to identify static or dynamic table index entries, to
// specify string lengths, and to update the size limit of the dynamic table.
// In QPACK, in addition to these uses, these integers also identify streams.
//
// The caller will need to validate that the decoded value is in an acceptable
// range.
//
// For details of the encoding, see:
//        http://httpwg.org/specs/rfc7541.html#integer.representation
//
// HpackVarintDecoder supports decoding any integer that can be represented on
// uint64_t, thereby exceeding the requirements for QPACK: "QPACK
// implementations MUST be able to decode integers up to 62 bits long."  See
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.1.1
//
// This decoder supports at most 10 extension bytes (bytes following the prefix,
// also called continuation bytes).  An encoder is allowed to zero pad the
// encoded integer on the left, thereby increasing the number of extension
// bytes.  If an encoder uses so much padding that the number of extension bytes
// exceeds the limit, then this decoder signals an error.

#ifndef QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_DECODER_H_
#define QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_DECODER_H_

#include <cstdint>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

// Sentinel value for |HpackVarintDecoder::offset_| to signify that decoding is
// completed.  Only used in debug builds.
#ifndef NDEBUG
const uint8_t kHpackVarintDecoderOffsetDone =
    std::numeric_limits<uint8_t>::max();
#endif

// Decodes an HPACK variable length unsigned integer, in a resumable fashion
// so it can handle running out of input in the DecodeBuffer. Call Start or
// StartExtended the first time (when decoding the byte that contains the
// prefix), then call Resume later if it is necessary to resume. When done,
// call value() to retrieve the decoded value.
//
// No constructor or destructor. Holds no resources, so destruction isn't
// needed. Start and StartExtended handles the initialization of member
// variables. This is necessary in order for HpackVarintDecoder to be part
// of a union.
class QUICHE_EXPORT_PRIVATE HpackVarintDecoder {
 public:
  // |prefix_value| is the first byte of the encoded varint.
  // |prefix_length| is number of bits in the first byte that are used for
  // encoding the integer.  |db| is the rest of the buffer,  that is, not
  // including the first byte.
  DecodeStatus Start(uint8_t prefix_value,
                     uint8_t prefix_length,
                     DecodeBuffer* db);

  // The caller has already determined that the encoding requires multiple
  // bytes, i.e. that the 3 to 8 low-order bits (the number determined by
  // |prefix_length|) of the first byte are are all 1.  |db| is the rest of the
  // buffer,  that is, not including the first byte.
  DecodeStatus StartExtended(uint8_t prefix_length, DecodeBuffer* db);

  // Resume decoding a variable length integer after an earlier
  // call to Start or StartExtended returned kDecodeInProgress.
  DecodeStatus Resume(DecodeBuffer* db);

  uint64_t value() const;

  // This supports optimizations for the case of a varint with zero extension
  // bytes, where the handling of the prefix is done by the caller.
  void set_value(uint64_t v);

  // All the public methods below are for supporting assertions and tests.

  std::string DebugString() const;

  // For benchmarking, these methods ensure the decoder
  // is NOT inlined into the caller.
  DecodeStatus StartForTest(uint8_t prefix_value,
                            uint8_t prefix_length,
                            DecodeBuffer* db);
  DecodeStatus StartExtendedForTest(uint8_t prefix_length, DecodeBuffer* db);
  DecodeStatus ResumeForTest(DecodeBuffer* db);

 private:
  // Protection in case Resume is called when it shouldn't be.
  void MarkDone() {
#ifndef NDEBUG
    offset_ = kHpackVarintDecoderOffsetDone;
#endif
  }
  void CheckNotDone() const {
#ifndef NDEBUG
    DCHECK_NE(kHpackVarintDecoderOffsetDone, offset_);
#endif
  }
  void CheckDone() const {
#ifndef NDEBUG
    DCHECK_EQ(kHpackVarintDecoderOffsetDone, offset_);
#endif
  }

  // These fields are initialized just to keep ASAN happy about reading
  // them from DebugString().

  // The encoded integer is being accumulated in |value_|.  When decoding is
  // complete, |value_| holds the result.
  uint64_t value_ = 0;

  // Each extension byte encodes in its lowest 7 bits a segment of the integer.
  // |offset_| is the number of places this segment has to be shifted to the
  // left for decoding.  It is zero for the first extension byte, and increases
  // by 7 for each subsequent extension byte.
  uint8_t offset_ = 0;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_VARINT_HPACK_VARINT_DECODER_H_
