// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"

#ifdef WIN32
#include <winsock2.h>  // for htonl() functions
#else
#include <arpa/inet.h>
#include <netinet/in.h>  // for htonl, htons
#endif

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"

namespace http2 {
namespace test {

Http2FrameBuilder::Http2FrameBuilder(Http2FrameType type,
                                     uint8_t flags,
                                     uint32_t stream_id) {
  AppendUInt24(0);  // Frame payload length, unknown so far.
  Append(type);
  AppendUInt8(flags);
  AppendUInt31(stream_id);
}

Http2FrameBuilder::Http2FrameBuilder(const Http2FrameHeader& v) {
  Append(v);
}

void Http2FrameBuilder::Append(quiche::QuicheStringPiece s) {
  Http2StrAppend(&buffer_, s);
}

void Http2FrameBuilder::AppendBytes(const void* data, uint32_t num_bytes) {
  Append(quiche::QuicheStringPiece(static_cast<const char*>(data), num_bytes));
}

void Http2FrameBuilder::AppendZeroes(size_t num_zero_bytes) {
  char zero = 0;
  buffer_.append(num_zero_bytes, zero);
}

void Http2FrameBuilder::AppendUInt8(uint8_t value) {
  AppendBytes(&value, 1);
}

void Http2FrameBuilder::AppendUInt16(uint16_t value) {
  value = htons(value);
  AppendBytes(&value, 2);
}

void Http2FrameBuilder::AppendUInt24(uint32_t value) {
  // Doesn't make sense to try to append a larger value, as that doesn't
  // simulate something an encoder could do (i.e. the other 8 bits simply aren't
  // there to be occupied).
  EXPECT_EQ(value, value & 0xffffff);
  value = htonl(value);
  AppendBytes(reinterpret_cast<char*>(&value) + 1, 3);
}

void Http2FrameBuilder::AppendUInt31(uint32_t value) {
  // If you want to test the high-bit being set, call AppendUInt32 instead.
  uint32_t tmp = value & StreamIdMask();
  EXPECT_EQ(value, value & StreamIdMask())
      << "High-bit of uint32_t should be clear.";
  value = htonl(tmp);
  AppendBytes(&value, 4);
}

void Http2FrameBuilder::AppendUInt32(uint32_t value) {
  value = htonl(value);
  AppendBytes(&value, sizeof(value));
}

void Http2FrameBuilder::Append(Http2ErrorCode error_code) {
  AppendUInt32(static_cast<uint32_t>(error_code));
}

void Http2FrameBuilder::Append(Http2FrameType type) {
  AppendUInt8(static_cast<uint8_t>(type));
}

void Http2FrameBuilder::Append(Http2SettingsParameter parameter) {
  AppendUInt16(static_cast<uint16_t>(parameter));
}

void Http2FrameBuilder::Append(const Http2FrameHeader& v) {
  AppendUInt24(v.payload_length);
  Append(v.type);
  AppendUInt8(v.flags);
  AppendUInt31(v.stream_id);
}

void Http2FrameBuilder::Append(const Http2PriorityFields& v) {
  // The EXCLUSIVE flag is the high-bit of the 32-bit stream dependency field.
  uint32_t tmp = v.stream_dependency & StreamIdMask();
  EXPECT_EQ(tmp, v.stream_dependency);
  if (v.is_exclusive) {
    tmp |= 0x80000000;
  }
  AppendUInt32(tmp);

  // The PRIORITY frame's weight field is logically in the range [1, 256],
  // but is encoded as a byte in the range [0, 255].
  ASSERT_LE(1u, v.weight);
  ASSERT_LE(v.weight, 256u);
  AppendUInt8(v.weight - 1);
}

void Http2FrameBuilder::Append(const Http2RstStreamFields& v) {
  Append(v.error_code);
}

void Http2FrameBuilder::Append(const Http2SettingFields& v) {
  Append(v.parameter);
  AppendUInt32(v.value);
}

void Http2FrameBuilder::Append(const Http2PushPromiseFields& v) {
  AppendUInt31(v.promised_stream_id);
}

void Http2FrameBuilder::Append(const Http2PingFields& v) {
  AppendBytes(v.opaque_bytes, sizeof Http2PingFields::opaque_bytes);
}

void Http2FrameBuilder::Append(const Http2GoAwayFields& v) {
  AppendUInt31(v.last_stream_id);
  Append(v.error_code);
}

void Http2FrameBuilder::Append(const Http2WindowUpdateFields& v) {
  EXPECT_NE(0u, v.window_size_increment) << "Increment must be non-zero.";
  AppendUInt31(v.window_size_increment);
}

void Http2FrameBuilder::Append(const Http2AltSvcFields& v) {
  AppendUInt16(v.origin_length);
}

// Methods for changing existing buffer contents.

void Http2FrameBuilder::WriteAt(quiche::QuicheStringPiece s, size_t offset) {
  ASSERT_LE(offset, buffer_.size());
  size_t len = offset + s.size();
  if (len > buffer_.size()) {
    buffer_.resize(len);
  }
  for (size_t ndx = 0; ndx < s.size(); ++ndx) {
    buffer_[offset + ndx] = s[ndx];
  }
}

void Http2FrameBuilder::WriteBytesAt(const void* data,
                                     uint32_t num_bytes,
                                     size_t offset) {
  WriteAt(quiche::QuicheStringPiece(static_cast<const char*>(data), num_bytes),
          offset);
}

void Http2FrameBuilder::WriteUInt24At(uint32_t value, size_t offset) {
  ASSERT_LT(value, static_cast<uint32_t>(1 << 24));
  value = htonl(value);
  WriteBytesAt(reinterpret_cast<char*>(&value) + 1, sizeof(value) - 1, offset);
}

void Http2FrameBuilder::SetPayloadLength(uint32_t payload_length) {
  WriteUInt24At(payload_length, 0);
}

size_t Http2FrameBuilder::SetPayloadLength() {
  EXPECT_GE(size(), Http2FrameHeader::EncodedSize());
  uint32_t payload_length = size() - Http2FrameHeader::EncodedSize();
  SetPayloadLength(payload_length);
  return payload_length;
}

}  // namespace test
}  // namespace http2
