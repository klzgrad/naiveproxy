// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_entry_type_decoder.h"

#include "base/logging.h"
#include "net/http2/platform/api/http2_string_utils.h"
#include "net/http2/tools/http2_bug_tracker.h"

namespace net {

Http2String HpackEntryTypeDecoder::DebugString() const {
  return Http2StrCat(
      "HpackEntryTypeDecoder(varint_decoder=", varint_decoder_.DebugString(),
      ", entry_type = ", entry_type_, ") ");
}

std::ostream& operator<<(std::ostream& out, const HpackEntryTypeDecoder& v) {
  return out << v.DebugString();
}

// This ridiculous looking function turned out to be the winner in benchmarking
// of several very different alternative implementations. It would be even
// faster (~7%) if inlined in the header file, but I'm not sure if that is
// worth doing... yet.
// TODO(jamessynge): Benchmark again at a higher level (e.g. at least at the
// full HTTP/2 decoder level, but preferably still higher) to determine if the
// alternatives that take less code/data space are preferable in that situation.
DecodeStatus HpackEntryTypeDecoder::Start(DecodeBuffer* db) {
  DCHECK(db != nullptr);
  DCHECK(db->HasData());

  // The high four bits (nibble) of first byte of the entry determine the type
  // of the entry, and may also be the initial bits of the varint that
  // represents an index or table size. Note the use of the word 'initial'
  // rather than 'high'; the HPACK encoding of varints is not in network
  // order (i.e. not big-endian, the high-order byte isn't first), nor in
  // little-endian order. See:
  // http://httpwg.org/specs/rfc7541.html#integer.representation
  uint8_t byte = db->DecodeUInt8();
  switch (byte) {
    case 0b00000000:
    case 0b00000001:
    case 0b00000010:
    case 0b00000011:
    case 0b00000100:
    case 0b00000101:
    case 0b00000110:
    case 0b00000111:
    case 0b00001000:
    case 0b00001001:
    case 0b00001010:
    case 0b00001011:
    case 0b00001100:
    case 0b00001101:
    case 0b00001110:
      // The low 4 bits of |byte| are the initial bits of the varint.
      // One of those bits is 0, so the varint is only one byte long.
      entry_type_ = HpackEntryType::kUnindexedLiteralHeader;
      varint_decoder_.set_value(byte);
      return DecodeStatus::kDecodeDone;

    case 0b00001111:
      // The low 4 bits of |byte| are the initial bits of the varint. All 4
      // are 1, so the varint extends into another byte.
      entry_type_ = HpackEntryType::kUnindexedLiteralHeader;
      return varint_decoder_.StartExtended(0x0f, db);

    case 0b00010000:
    case 0b00010001:
    case 0b00010010:
    case 0b00010011:
    case 0b00010100:
    case 0b00010101:
    case 0b00010110:
    case 0b00010111:
    case 0b00011000:
    case 0b00011001:
    case 0b00011010:
    case 0b00011011:
    case 0b00011100:
    case 0b00011101:
    case 0b00011110:
      // The low 4 bits of |byte| are the initial bits of the varint.
      // One of those bits is 0, so the varint is only one byte long.
      entry_type_ = HpackEntryType::kNeverIndexedLiteralHeader;
      varint_decoder_.set_value(byte & 0x0f);
      return DecodeStatus::kDecodeDone;

    case 0b00011111:
      // The low 4 bits of |byte| are the initial bits of the varint.
      // All of those bits are 1, so the varint extends into another byte.
      entry_type_ = HpackEntryType::kNeverIndexedLiteralHeader;
      return varint_decoder_.StartExtended(0x0f, db);

    case 0b00100000:
    case 0b00100001:
    case 0b00100010:
    case 0b00100011:
    case 0b00100100:
    case 0b00100101:
    case 0b00100110:
    case 0b00100111:
    case 0b00101000:
    case 0b00101001:
    case 0b00101010:
    case 0b00101011:
    case 0b00101100:
    case 0b00101101:
    case 0b00101110:
    case 0b00101111:
    case 0b00110000:
    case 0b00110001:
    case 0b00110010:
    case 0b00110011:
    case 0b00110100:
    case 0b00110101:
    case 0b00110110:
    case 0b00110111:
    case 0b00111000:
    case 0b00111001:
    case 0b00111010:
    case 0b00111011:
    case 0b00111100:
    case 0b00111101:
    case 0b00111110:
      entry_type_ = HpackEntryType::kDynamicTableSizeUpdate;
      // The low 5 bits of |byte| are the initial bits of the varint.
      // One of those bits is 0, so the varint is only one byte long.
      varint_decoder_.set_value(byte & 0x01f);
      return DecodeStatus::kDecodeDone;

    case 0b00111111:
      entry_type_ = HpackEntryType::kDynamicTableSizeUpdate;
      // The low 5 bits of |byte| are the initial bits of the varint.
      // All of those bits are 1, so the varint extends into another byte.
      return varint_decoder_.StartExtended(0x1f, db);

    case 0b01000000:
    case 0b01000001:
    case 0b01000010:
    case 0b01000011:
    case 0b01000100:
    case 0b01000101:
    case 0b01000110:
    case 0b01000111:
    case 0b01001000:
    case 0b01001001:
    case 0b01001010:
    case 0b01001011:
    case 0b01001100:
    case 0b01001101:
    case 0b01001110:
    case 0b01001111:
    case 0b01010000:
    case 0b01010001:
    case 0b01010010:
    case 0b01010011:
    case 0b01010100:
    case 0b01010101:
    case 0b01010110:
    case 0b01010111:
    case 0b01011000:
    case 0b01011001:
    case 0b01011010:
    case 0b01011011:
    case 0b01011100:
    case 0b01011101:
    case 0b01011110:
    case 0b01011111:
    case 0b01100000:
    case 0b01100001:
    case 0b01100010:
    case 0b01100011:
    case 0b01100100:
    case 0b01100101:
    case 0b01100110:
    case 0b01100111:
    case 0b01101000:
    case 0b01101001:
    case 0b01101010:
    case 0b01101011:
    case 0b01101100:
    case 0b01101101:
    case 0b01101110:
    case 0b01101111:
    case 0b01110000:
    case 0b01110001:
    case 0b01110010:
    case 0b01110011:
    case 0b01110100:
    case 0b01110101:
    case 0b01110110:
    case 0b01110111:
    case 0b01111000:
    case 0b01111001:
    case 0b01111010:
    case 0b01111011:
    case 0b01111100:
    case 0b01111101:
    case 0b01111110:
      entry_type_ = HpackEntryType::kIndexedLiteralHeader;
      // The low 6 bits of |byte| are the initial bits of the varint.
      // One of those bits is 0, so the varint is only one byte long.
      varint_decoder_.set_value(byte & 0x03f);
      return DecodeStatus::kDecodeDone;

    case 0b01111111:
      entry_type_ = HpackEntryType::kIndexedLiteralHeader;
      // The low 6 bits of |byte| are the initial bits of the varint.
      // All of those bits are 1, so the varint extends into another byte.
      return varint_decoder_.StartExtended(0x3f, db);

    case 0b10000000:
    case 0b10000001:
    case 0b10000010:
    case 0b10000011:
    case 0b10000100:
    case 0b10000101:
    case 0b10000110:
    case 0b10000111:
    case 0b10001000:
    case 0b10001001:
    case 0b10001010:
    case 0b10001011:
    case 0b10001100:
    case 0b10001101:
    case 0b10001110:
    case 0b10001111:
    case 0b10010000:
    case 0b10010001:
    case 0b10010010:
    case 0b10010011:
    case 0b10010100:
    case 0b10010101:
    case 0b10010110:
    case 0b10010111:
    case 0b10011000:
    case 0b10011001:
    case 0b10011010:
    case 0b10011011:
    case 0b10011100:
    case 0b10011101:
    case 0b10011110:
    case 0b10011111:
    case 0b10100000:
    case 0b10100001:
    case 0b10100010:
    case 0b10100011:
    case 0b10100100:
    case 0b10100101:
    case 0b10100110:
    case 0b10100111:
    case 0b10101000:
    case 0b10101001:
    case 0b10101010:
    case 0b10101011:
    case 0b10101100:
    case 0b10101101:
    case 0b10101110:
    case 0b10101111:
    case 0b10110000:
    case 0b10110001:
    case 0b10110010:
    case 0b10110011:
    case 0b10110100:
    case 0b10110101:
    case 0b10110110:
    case 0b10110111:
    case 0b10111000:
    case 0b10111001:
    case 0b10111010:
    case 0b10111011:
    case 0b10111100:
    case 0b10111101:
    case 0b10111110:
    case 0b10111111:
    case 0b11000000:
    case 0b11000001:
    case 0b11000010:
    case 0b11000011:
    case 0b11000100:
    case 0b11000101:
    case 0b11000110:
    case 0b11000111:
    case 0b11001000:
    case 0b11001001:
    case 0b11001010:
    case 0b11001011:
    case 0b11001100:
    case 0b11001101:
    case 0b11001110:
    case 0b11001111:
    case 0b11010000:
    case 0b11010001:
    case 0b11010010:
    case 0b11010011:
    case 0b11010100:
    case 0b11010101:
    case 0b11010110:
    case 0b11010111:
    case 0b11011000:
    case 0b11011001:
    case 0b11011010:
    case 0b11011011:
    case 0b11011100:
    case 0b11011101:
    case 0b11011110:
    case 0b11011111:
    case 0b11100000:
    case 0b11100001:
    case 0b11100010:
    case 0b11100011:
    case 0b11100100:
    case 0b11100101:
    case 0b11100110:
    case 0b11100111:
    case 0b11101000:
    case 0b11101001:
    case 0b11101010:
    case 0b11101011:
    case 0b11101100:
    case 0b11101101:
    case 0b11101110:
    case 0b11101111:
    case 0b11110000:
    case 0b11110001:
    case 0b11110010:
    case 0b11110011:
    case 0b11110100:
    case 0b11110101:
    case 0b11110110:
    case 0b11110111:
    case 0b11111000:
    case 0b11111001:
    case 0b11111010:
    case 0b11111011:
    case 0b11111100:
    case 0b11111101:
    case 0b11111110:
      entry_type_ = HpackEntryType::kIndexedHeader;
      // The low 7 bits of |byte| are the initial bits of the varint.
      // One of those bits is 0, so the varint is only one byte long.
      varint_decoder_.set_value(byte & 0x07f);
      return DecodeStatus::kDecodeDone;

    case 0b11111111:
      entry_type_ = HpackEntryType::kIndexedHeader;
      // The low 7 bits of |byte| are the initial bits of the varint.
      // All of those bits are 1, so the varint extends into another byte.
      return varint_decoder_.StartExtended(0x7f, db);
  }
  HTTP2_BUG << "Unreachable, byte=" << std::hex << static_cast<uint32_t>(byte);
  return DecodeStatus::kDecodeError;
}

}  // namespace net
