// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/tools/quic_http_frame_builder.h"

#ifdef WIN32
#include <winsock2.h>  // for htonl() functions
#else
#include <arpa/inet.h>   // IWYU pragma: keep  // because of Chrome
#include <netinet/in.h>  // for htonl, htons
#endif

#include "net/quic/platform/api/quic_string_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

QuicHttpFrameBuilder::QuicHttpFrameBuilder(QuicHttpFrameType type,
                                           uint8_t flags,
                                           uint32_t stream_id) {
  AppendUInt24(0);  // Frame payload length, unknown so far.
  Append(type);
  AppendUInt8(flags);
  AppendUInt31(stream_id);
}

QuicHttpFrameBuilder::QuicHttpFrameBuilder(const QuicHttpFrameHeader& v) {
  Append(v);
}

void QuicHttpFrameBuilder::Append(QuicStringPiece s) {
  QuicStrAppend(&buffer_, s);
}

void QuicHttpFrameBuilder::AppendBytes(const void* data, uint32_t num_bytes) {
  Append(QuicStringPiece(static_cast<const char*>(data), num_bytes));
}

void QuicHttpFrameBuilder::AppendZeroes(size_t num_zero_bytes) {
  char zero = 0;
  buffer_.append(num_zero_bytes, zero);
}

void QuicHttpFrameBuilder::AppendUInt8(uint8_t value) {
  AppendBytes(&value, 1);
}

void QuicHttpFrameBuilder::AppendUInt16(uint16_t value) {
  value = htons(value);
  AppendBytes(&value, 2);
}

void QuicHttpFrameBuilder::AppendUInt24(uint32_t value) {
  // Doesn't make sense to try to append a larger value, as that doesn't
  // simulate something an encoder could do (i.e. the other 8 bits simply aren't
  // there to be occupied).
  EXPECT_EQ(value, value & 0xffffff);
  value = htonl(value);
  AppendBytes(reinterpret_cast<char*>(&value) + 1, 3);
}

void QuicHttpFrameBuilder::AppendUInt31(uint32_t value) {
  // If you want to test the high-bit being set, call AppendUInt32 instead.
  uint32_t tmp = value & QuicHttpStreamIdMask();
  EXPECT_EQ(value, value & QuicHttpStreamIdMask())
      << "High-bit of uint32_t should be clear.";
  value = htonl(tmp);
  AppendBytes(&value, 4);
}

void QuicHttpFrameBuilder::AppendUInt32(uint32_t value) {
  value = htonl(value);
  AppendBytes(&value, sizeof(value));
}

void QuicHttpFrameBuilder::Append(QuicHttpErrorCode error_code) {
  AppendUInt32(static_cast<uint32_t>(error_code));
}

void QuicHttpFrameBuilder::Append(QuicHttpFrameType type) {
  AppendUInt8(static_cast<uint8_t>(type));
}

void QuicHttpFrameBuilder::Append(QuicHttpSettingsParameter parameter) {
  AppendUInt16(static_cast<uint16_t>(parameter));
}

void QuicHttpFrameBuilder::Append(const QuicHttpFrameHeader& v) {
  AppendUInt24(v.payload_length);
  Append(v.type);
  AppendUInt8(v.flags);
  AppendUInt31(v.stream_id);
}

void QuicHttpFrameBuilder::Append(const QuicHttpPriorityFields& v) {
  // The EXCLUSIVE flag is the high-bit of the 32-bit stream dependency field.
  uint32_t tmp = v.stream_dependency & QuicHttpStreamIdMask();
  EXPECT_EQ(tmp, v.stream_dependency);
  if (v.is_exclusive) {
    tmp |= 0x80000000;
  }
  AppendUInt32(tmp);

  // The QUIC_HTTP_PRIORITY frame's weight field is logically in the range [1,
  // 256], but is encoded as a byte in the range [0, 255].
  ASSERT_LE(1u, v.weight);
  ASSERT_LE(v.weight, 256u);
  AppendUInt8(v.weight - 1);
}

void QuicHttpFrameBuilder::Append(const QuicHttpRstStreamFields& v) {
  Append(v.error_code);
}

void QuicHttpFrameBuilder::Append(const QuicHttpSettingFields& v) {
  Append(v.parameter);
  AppendUInt32(v.value);
}

void QuicHttpFrameBuilder::Append(const QuicHttpPushPromiseFields& v) {
  AppendUInt31(v.promised_stream_id);
}

void QuicHttpFrameBuilder::Append(const QuicHttpPingFields& v) {
  AppendBytes(v.opaque_bytes, sizeof QuicHttpPingFields::opaque_bytes);
}

void QuicHttpFrameBuilder::Append(const QuicHttpGoAwayFields& v) {
  AppendUInt31(v.last_stream_id);
  Append(v.error_code);
}

void QuicHttpFrameBuilder::Append(const QuicHttpWindowUpdateFields& v) {
  EXPECT_NE(0u, v.window_size_increment) << "Increment must be non-zero.";
  AppendUInt31(v.window_size_increment);
}

void QuicHttpFrameBuilder::Append(const QuicHttpAltSvcFields& v) {
  AppendUInt16(v.origin_length);
}

// Methods for changing existing buffer contents.

void QuicHttpFrameBuilder::WriteAt(QuicStringPiece s, size_t offset) {
  ASSERT_LE(offset, buffer_.size());
  size_t len = offset + s.size();
  if (len > buffer_.size()) {
    buffer_.resize(len);
  }
  for (size_t ndx = 0; ndx < s.size(); ++ndx) {
    buffer_[offset + ndx] = s[ndx];
  }
}

void QuicHttpFrameBuilder::WriteBytesAt(const void* data,
                                        uint32_t num_bytes,
                                        size_t offset) {
  WriteAt(QuicStringPiece(static_cast<const char*>(data), num_bytes), offset);
}

void QuicHttpFrameBuilder::WriteUInt24At(uint32_t value, size_t offset) {
  ASSERT_LT(value, 1u << 24);
  value = htonl(value);
  WriteBytesAt(reinterpret_cast<char*>(&value) + 1, sizeof(value) - 1, offset);
}

void QuicHttpFrameBuilder::SetPayloadLength(uint32_t payload_length) {
  WriteUInt24At(payload_length, 0);
}

size_t QuicHttpFrameBuilder::SetPayloadLength() {
  EXPECT_GE(size(), QuicHttpFrameHeader::EncodedSize());
  uint32_t payload_length = size() - QuicHttpFrameHeader::EncodedSize();
  SetPayloadLength(payload_length);
  return payload_length;
}

}  // namespace test
}  // namespace net
