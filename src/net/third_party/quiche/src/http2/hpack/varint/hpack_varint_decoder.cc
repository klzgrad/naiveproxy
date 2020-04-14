// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_decoder.h"

#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace http2 {

DecodeStatus HpackVarintDecoder::Start(uint8_t prefix_value,
                                       uint8_t prefix_length,
                                       DecodeBuffer* db) {
  DCHECK_LE(3u, prefix_length);
  DCHECK_LE(prefix_length, 8u);

  // |prefix_mask| defines the sequence of low-order bits of the first byte
  // that encode the prefix of the value. It is also the marker in those bits
  // of the first byte indicating that at least one extension byte is needed.
  const uint8_t prefix_mask = (1 << prefix_length) - 1;

  // Ignore the bits that aren't a part of the prefix of the varint.
  value_ = prefix_value & prefix_mask;

  if (value_ < prefix_mask) {
    MarkDone();
    return DecodeStatus::kDecodeDone;
  }

  offset_ = 0;
  return Resume(db);
}

DecodeStatus HpackVarintDecoder::StartExtended(uint8_t prefix_length,
                                               DecodeBuffer* db) {
  DCHECK_LE(3u, prefix_length);
  DCHECK_LE(prefix_length, 8u);

  value_ = (1 << prefix_length) - 1;
  offset_ = 0;
  return Resume(db);
}

DecodeStatus HpackVarintDecoder::Resume(DecodeBuffer* db) {
  // There can be at most 10 continuation bytes.  Offset is zero for the
  // first one and increases by 7 for each subsequent one.
  const uint8_t kMaxOffset = 63;
  CheckNotDone();

  // Process most extension bytes without the need for overflow checking.
  while (offset_ < kMaxOffset) {
    if (db->Empty()) {
      return DecodeStatus::kDecodeInProgress;
    }

    uint8_t byte = db->DecodeUInt8();
    uint64_t summand = byte & 0x7f;

    // Shifting a 7 bit value to the left by at most 56 places can never
    // overflow on uint64_t.
    DCHECK_LE(offset_, 56);
    DCHECK_LE(summand, std::numeric_limits<uint64_t>::max() >> offset_);

    summand <<= offset_;

    // At this point,
    // |value_| is at most (2^prefix_length - 1) + (2^49 - 1), and
    // |summand| is at most 255 << 56 (which is smaller than 2^63),
    // so adding them can never overflow on uint64_t.
    DCHECK_LE(value_, std::numeric_limits<uint64_t>::max() - summand);

    value_ += summand;

    // Decoding ends if continuation flag is not set.
    if ((byte & 0x80) == 0) {
      MarkDone();
      return DecodeStatus::kDecodeDone;
    }

    offset_ += 7;
  }

  if (db->Empty()) {
    return DecodeStatus::kDecodeInProgress;
  }

  DCHECK_EQ(kMaxOffset, offset_);

  uint8_t byte = db->DecodeUInt8();
  // No more extension bytes are allowed after this.
  if ((byte & 0x80) == 0) {
    uint64_t summand = byte & 0x7f;
    // Check for overflow in left shift.
    if (summand <= std::numeric_limits<uint64_t>::max() >> offset_) {
      summand <<= offset_;
      // Check for overflow in addition.
      if (value_ <= std::numeric_limits<uint64_t>::max() - summand) {
        value_ += summand;
        MarkDone();
        return DecodeStatus::kDecodeDone;
      }
    }
  }

  // Signal error if value is too large or there are too many extension bytes.
  HTTP2_DLOG(WARNING)
      << "Variable length int encoding is too large or too long. "
      << DebugString();
  MarkDone();
  return DecodeStatus::kDecodeError;
}

uint64_t HpackVarintDecoder::value() const {
  CheckDone();
  return value_;
}

void HpackVarintDecoder::set_value(uint64_t v) {
  MarkDone();
  value_ = v;
}

std::string HpackVarintDecoder::DebugString() const {
  return quiche::QuicheStrCat("HpackVarintDecoder(value=", value_,
                              ", offset=", offset_, ")");
}

DecodeStatus HpackVarintDecoder::StartForTest(uint8_t prefix_value,
                                              uint8_t prefix_length,
                                              DecodeBuffer* db) {
  return Start(prefix_value, prefix_length, db);
}

DecodeStatus HpackVarintDecoder::StartExtendedForTest(uint8_t prefix_length,
                                                      DecodeBuffer* db) {
  return StartExtended(prefix_length, db);
}

DecodeStatus HpackVarintDecoder::ResumeForTest(DecodeBuffer* db) {
  return Resume(db);
}

}  // namespace http2
