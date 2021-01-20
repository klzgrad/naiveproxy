// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/http2_constants_test_util.h"

namespace http2 {
namespace test {

std::vector<Http2ErrorCode> AllHttp2ErrorCodes() {
  // clang-format off
  return {
      Http2ErrorCode::HTTP2_NO_ERROR,
      Http2ErrorCode::PROTOCOL_ERROR,
      Http2ErrorCode::INTERNAL_ERROR,
      Http2ErrorCode::FLOW_CONTROL_ERROR,
      Http2ErrorCode::SETTINGS_TIMEOUT,
      Http2ErrorCode::STREAM_CLOSED,
      Http2ErrorCode::FRAME_SIZE_ERROR,
      Http2ErrorCode::REFUSED_STREAM,
      Http2ErrorCode::CANCEL,
      Http2ErrorCode::COMPRESSION_ERROR,
      Http2ErrorCode::CONNECT_ERROR,
      Http2ErrorCode::ENHANCE_YOUR_CALM,
      Http2ErrorCode::INADEQUATE_SECURITY,
      Http2ErrorCode::HTTP_1_1_REQUIRED,
  };
  // clang-format on
}

std::vector<Http2SettingsParameter> AllHttp2SettingsParameters() {
  // clang-format off
  return {
      Http2SettingsParameter::HEADER_TABLE_SIZE,
      Http2SettingsParameter::ENABLE_PUSH,
      Http2SettingsParameter::MAX_CONCURRENT_STREAMS,
      Http2SettingsParameter::INITIAL_WINDOW_SIZE,
      Http2SettingsParameter::MAX_FRAME_SIZE,
      Http2SettingsParameter::MAX_HEADER_LIST_SIZE,
  };
  // clang-format on
}

// Returns a mask of flags supported for the specified frame type. Returns
// zero for unknown frame types.
uint8_t KnownFlagsMaskForFrameType(Http2FrameType type) {
  switch (type) {
    case Http2FrameType::DATA:
      return Http2FrameFlag::END_STREAM | Http2FrameFlag::PADDED;
    case Http2FrameType::HEADERS:
      return Http2FrameFlag::END_STREAM | Http2FrameFlag::END_HEADERS |
             Http2FrameFlag::PADDED | Http2FrameFlag::PRIORITY;
    case Http2FrameType::PRIORITY:
      return 0x00;
    case Http2FrameType::RST_STREAM:
      return 0x00;
    case Http2FrameType::SETTINGS:
      return Http2FrameFlag::ACK;
    case Http2FrameType::PUSH_PROMISE:
      return Http2FrameFlag::END_HEADERS | Http2FrameFlag::PADDED;
    case Http2FrameType::PING:
      return Http2FrameFlag::ACK;
    case Http2FrameType::GOAWAY:
      return 0x00;
    case Http2FrameType::WINDOW_UPDATE:
      return 0x00;
    case Http2FrameType::CONTINUATION:
      return Http2FrameFlag::END_HEADERS;
    case Http2FrameType::ALTSVC:
      return 0x00;
    default:
      return 0x00;
  }
}

uint8_t InvalidFlagMaskForFrameType(Http2FrameType type) {
  if (IsSupportedHttp2FrameType(type)) {
    return ~KnownFlagsMaskForFrameType(type);
  }
  return 0x00;
}

}  // namespace test
}  // namespace http2
