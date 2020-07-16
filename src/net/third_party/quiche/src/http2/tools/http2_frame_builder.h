// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TOOLS_HTTP2_FRAME_BUILDER_H_
#define QUICHE_HTTP2_TOOLS_HTTP2_FRAME_BUILDER_H_

// Http2FrameBuilder builds wire-format HTTP/2 frames (or fragments thereof)
// from components.
//
// For now, this is only intended for use in tests, and thus has EXPECT* in the
// code. If desired to use it in an encoder, it will need optimization work,
// especially w.r.t memory mgmt, and the EXPECT* will need to be removed or
// replaced with DCHECKs.

#include <stddef.h>  // for size_t

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {
namespace test {

class Http2FrameBuilder {
 public:
  Http2FrameBuilder(Http2FrameType type, uint8_t flags, uint32_t stream_id);
  explicit Http2FrameBuilder(const Http2FrameHeader& v);
  Http2FrameBuilder() {}
  ~Http2FrameBuilder() {}

  size_t size() const { return buffer_.size(); }
  const std::string& buffer() const { return buffer_; }

  //----------------------------------------------------------------------------
  // Methods for appending to the end of the buffer.

  // Append a sequence of bytes from various sources.
  void Append(quiche::QuicheStringPiece s);
  void AppendBytes(const void* data, uint32_t num_bytes);

  // Append an array of type T[N] to the string. Intended for tests with arrays
  // initialized from literals, such as:
  //    const char kData[] = {0x12, 0x23, ...};
  //    builder.AppendBytes(kData);
  template <typename T, size_t N>
  void AppendBytes(T (&buf)[N]) {
    AppendBytes(buf, N * sizeof(buf[0]));
  }

  // Support for appending padding. Does not read or write the Pad Length field.
  void AppendZeroes(size_t num_zero_bytes);

  // Append various sizes of unsigned integers.
  void AppendUInt8(uint8_t value);
  void AppendUInt16(uint16_t value);
  void AppendUInt24(uint32_t value);
  void AppendUInt31(uint32_t value);
  void AppendUInt32(uint32_t value);

  // Append various enums.
  void Append(Http2ErrorCode error_code);
  void Append(Http2FrameType type);
  void Append(Http2SettingsParameter parameter);

  // Append various structures.
  void Append(const Http2FrameHeader& v);
  void Append(const Http2PriorityFields& v);
  void Append(const Http2RstStreamFields& v);
  void Append(const Http2SettingFields& v);
  void Append(const Http2PushPromiseFields& v);
  void Append(const Http2PingFields& v);
  void Append(const Http2GoAwayFields& v);
  void Append(const Http2WindowUpdateFields& v);
  void Append(const Http2AltSvcFields& v);

  // Methods for changing existing buffer contents (mostly focused on updating
  // the payload length).

  void WriteAt(quiche::QuicheStringPiece s, size_t offset);
  void WriteBytesAt(const void* data, uint32_t num_bytes, size_t offset);
  void WriteUInt24At(uint32_t value, size_t offset);

  // Set the payload length to the specified size.
  void SetPayloadLength(uint32_t payload_length);

  // Sets the payload length to the size of the buffer minus the size of
  // the frame header.
  size_t SetPayloadLength();

 private:
  std::string buffer_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TOOLS_HTTP2_FRAME_BUILDER_H_
