// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/http2_constants.h"

#include <ios>
#include <sstream>

#include "base/logging.h"
#include "net/http2/platform/api/http2_string_piece.h"
#include "net/http2/platform/api/http2_string_utils.h"

namespace net {

Http2String Http2FrameTypeToString(Http2FrameType v) {
  switch (v) {
    case Http2FrameType::DATA:
      return "DATA";
    case Http2FrameType::HEADERS:
      return "HEADERS";
    case Http2FrameType::PRIORITY:
      return "PRIORITY";
    case Http2FrameType::RST_STREAM:
      return "RST_STREAM";
    case Http2FrameType::SETTINGS:
      return "SETTINGS";
    case Http2FrameType::PUSH_PROMISE:
      return "PUSH_PROMISE";
    case Http2FrameType::PING:
      return "PING";
    case Http2FrameType::GOAWAY:
      return "GOAWAY";
    case Http2FrameType::WINDOW_UPDATE:
      return "WINDOW_UPDATE";
    case Http2FrameType::CONTINUATION:
      return "CONTINUATION";
    case Http2FrameType::ALTSVC:
      return "ALTSVC";
  }
  return Http2StrCat("UnknownFrameType(", static_cast<int>(v), ")");
}

Http2String Http2FrameTypeToString(uint8_t v) {
  return Http2FrameTypeToString(static_cast<Http2FrameType>(v));
}

Http2String Http2FrameFlagsToString(Http2FrameType type, uint8_t flags) {
  Http2String s;
  // Closure to append flag name |v| to the Http2String |s|,
  // and to clear |bit| from |flags|.
  auto append_and_clear = [&s, &flags](Http2StringPiece v, uint8_t bit) {
    if (!s.empty()) {
      s.push_back('|');
    }
    Http2StrAppend(&s, v);
    flags ^= bit;
  };
  if (flags & 0x01) {
    if (type == Http2FrameType::DATA || type == Http2FrameType::HEADERS) {
      append_and_clear("END_STREAM", Http2FrameFlag::END_STREAM);
    } else if (type == Http2FrameType::SETTINGS ||
               type == Http2FrameType::PING) {
      append_and_clear("ACK", Http2FrameFlag::ACK);
    }
  }
  if (flags & 0x04) {
    if (type == Http2FrameType::HEADERS ||
        type == Http2FrameType::PUSH_PROMISE ||
        type == Http2FrameType::CONTINUATION) {
      append_and_clear("END_HEADERS", Http2FrameFlag::END_HEADERS);
    }
  }
  if (flags & 0x08) {
    if (type == Http2FrameType::DATA || type == Http2FrameType::HEADERS ||
        type == Http2FrameType::PUSH_PROMISE) {
      append_and_clear("PADDED", Http2FrameFlag::PADDED);
    }
  }
  if (flags & 0x20) {
    if (type == Http2FrameType::HEADERS) {
      append_and_clear("PRIORITY", Http2FrameFlag::PRIORITY);
    }
  }
  if (flags != 0) {
    append_and_clear(Http2StringPrintf("0x%02x", flags), flags);
  }
  DCHECK_EQ(0, flags);
  return s;
}
Http2String Http2FrameFlagsToString(uint8_t type, uint8_t flags) {
  return Http2FrameFlagsToString(static_cast<Http2FrameType>(type), flags);
}

Http2String Http2ErrorCodeToString(uint32_t v) {
  switch (v) {
    case 0x0:
      return "NO_ERROR";
    case 0x1:
      return "PROTOCOL_ERROR";
    case 0x2:
      return "INTERNAL_ERROR";
    case 0x3:
      return "FLOW_CONTROL_ERROR";
    case 0x4:
      return "SETTINGS_TIMEOUT";
    case 0x5:
      return "STREAM_CLOSED";
    case 0x6:
      return "FRAME_SIZE_ERROR";
    case 0x7:
      return "REFUSED_STREAM";
    case 0x8:
      return "CANCEL";
    case 0x9:
      return "COMPRESSION_ERROR";
    case 0xa:
      return "CONNECT_ERROR";
    case 0xb:
      return "ENHANCE_YOUR_CALM";
    case 0xc:
      return "INADEQUATE_SECURITY";
    case 0xd:
      return "HTTP_1_1_REQUIRED";
  }
  std::stringstream ss;
  ss << "UnknownErrorCode(0x" << std::hex << v << ")";
  return ss.str();
}
Http2String Http2ErrorCodeToString(Http2ErrorCode v) {
  return Http2ErrorCodeToString(static_cast<uint32_t>(v));
}

Http2String Http2SettingsParameterToString(uint32_t v) {
  switch (v) {
    case 0x1:
      return "HEADER_TABLE_SIZE";
    case 0x2:
      return "ENABLE_PUSH";
    case 0x3:
      return "MAX_CONCURRENT_STREAMS";
    case 0x4:
      return "INITIAL_WINDOW_SIZE";
    case 0x5:
      return "MAX_FRAME_SIZE";
    case 0x6:
      return "MAX_HEADER_LIST_SIZE";
  }
  std::stringstream ss;
  ss << "UnknownSettingsParameter(0x" << std::hex << v << ")";
  return ss.str();
}
Http2String Http2SettingsParameterToString(Http2SettingsParameter v) {
  return Http2SettingsParameterToString(static_cast<uint32_t>(v));
}

}  // namespace net
