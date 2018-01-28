// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_varint_decoder.h"

#include "net/http2/platform/api/http2_string_utils.h"

namespace net {

DecodeStatus HpackVarintDecoder::Start(uint8_t prefix_value,
                                       uint8_t prefix_mask,
                                       DecodeBuffer* db) {
  DCHECK_LE(15, prefix_mask) << std::hex << prefix_mask;
  DCHECK_LE(prefix_mask, 127) << std::hex << prefix_mask;
  // Confirm that |prefix_mask| is a contiguous sequence of bits.
  DCHECK_EQ(0, (prefix_mask + 1) & prefix_mask) << std::hex << prefix_mask;

  // Ignore the bits that aren't a part of the prefix of the varint.
  value_ = prefix_value & prefix_mask;

  if (value_ < prefix_mask) {
    MarkDone();
    return DecodeStatus::kDecodeDone;
  }

  offset_ = 0;
  return Resume(db);
}

DecodeStatus HpackVarintDecoder::StartExtended(uint8_t prefix_mask,
                                               DecodeBuffer* db) {
  DCHECK_LE(15, prefix_mask) << std::hex << prefix_mask;
  DCHECK_LE(prefix_mask, 127) << std::hex << prefix_mask;
  // Confirm that |prefix_mask| is a contiguous sequence of bits.
  DCHECK_EQ(0, prefix_mask & (prefix_mask + 1)) << std::hex << prefix_mask;

  value_ = prefix_mask;
  offset_ = 0;
  return Resume(db);
}

DecodeStatus HpackVarintDecoder::Resume(DecodeBuffer* db) {
  CheckNotDone();
  do {
    if (db->Empty()) {
      return DecodeStatus::kDecodeInProgress;
    }
    uint8_t byte = db->DecodeUInt8();
    if (offset_ == MaxOffset() && byte != 0)
      break;
    value_ += (byte & 0x7f) << offset_;
    if ((byte & 0x80) == 0) {
      MarkDone();
      return DecodeStatus::kDecodeDone;
    }
    offset_ += 7;
  } while (offset_ <= MaxOffset());
  DLOG(WARNING) << "Variable length int encoding is too large or too long. "
                << DebugString();
  MarkDone();
  return DecodeStatus::kDecodeError;
}

uint32_t HpackVarintDecoder::value() const {
  CheckDone();
  return value_;
}

void HpackVarintDecoder::set_value(uint32_t v) {
  MarkDone();
  value_ = v;
}

Http2String HpackVarintDecoder::DebugString() const {
  return Http2StrCat("HpackVarintDecoder(value=", value_, ", offset=", offset_,
                     ")");
}

DecodeStatus HpackVarintDecoder::StartForTest(uint8_t prefix_value,
                                              uint8_t prefix_mask,
                                              DecodeBuffer* db) {
  return Start(prefix_value, prefix_mask, db);
}

DecodeStatus HpackVarintDecoder::StartExtendedForTest(uint8_t prefix_mask,
                                                      DecodeBuffer* db) {
  return StartExtended(prefix_mask, db);
}

DecodeStatus HpackVarintDecoder::ResumeForTest(DecodeBuffer* db) {
  return Resume(db);
}

}  // namespace net
