// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/quic_http_constants.h"

#include "base/logging.h"
#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/quic/platform/api/quic_string_utils.h"
#include "net/quic/platform/api/quic_text_utils.h"

namespace net {

QuicString QuicHttpFrameTypeToString(QuicHttpFrameType v) {
  switch (v) {
    case QuicHttpFrameType::DATA:
      return "DATA";
    case QuicHttpFrameType::HEADERS:
      return "HEADERS";
    case QuicHttpFrameType::QUIC_HTTP_PRIORITY:
      return "QUIC_HTTP_PRIORITY";
    case QuicHttpFrameType::RST_STREAM:
      return "RST_STREAM";
    case QuicHttpFrameType::SETTINGS:
      return "SETTINGS";
    case QuicHttpFrameType::PUSH_PROMISE:
      return "PUSH_PROMISE";
    case QuicHttpFrameType::PING:
      return "PING";
    case QuicHttpFrameType::GOAWAY:
      return "GOAWAY";
    case QuicHttpFrameType::WINDOW_UPDATE:
      return "WINDOW_UPDATE";
    case QuicHttpFrameType::CONTINUATION:
      return "CONTINUATION";
    case QuicHttpFrameType::ALTSVC:
      return "ALTSVC";
  }
  return QuicStrCat("UnknownFrameType(", static_cast<int>(v), ")");
}

QuicString QuicHttpFrameTypeToString(uint8_t v) {
  return QuicHttpFrameTypeToString(static_cast<QuicHttpFrameType>(v));
}

QuicString QuicHttpFrameFlagsToString(QuicHttpFrameType type, uint8_t flags) {
  QuicString s;
  // Closure to append flag name |v| to the QuicString |s|,
  // and to clear |bit| from |flags|.
  auto append_and_clear = [&s, &flags](QuicStringPiece v, uint8_t bit) {
    if (!s.empty()) {
      s.push_back('|');
    }
    QuicStrAppend(&s, v);
    flags ^= bit;
  };
  if (flags & 0x01) {
    if (type == QuicHttpFrameType::DATA || type == QuicHttpFrameType::HEADERS) {
      append_and_clear("QUIC_HTTP_END_STREAM",
                       QuicHttpFrameFlag::QUIC_HTTP_END_STREAM);
    } else if (type == QuicHttpFrameType::SETTINGS ||
               type == QuicHttpFrameType::PING) {
      append_and_clear("QUIC_HTTP_ACK", QuicHttpFrameFlag::QUIC_HTTP_ACK);
    }
  }
  if (flags & 0x04) {
    if (type == QuicHttpFrameType::HEADERS ||
        type == QuicHttpFrameType::PUSH_PROMISE ||
        type == QuicHttpFrameType::CONTINUATION) {
      append_and_clear("QUIC_HTTP_END_HEADERS",
                       QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS);
    }
  }
  if (flags & 0x08) {
    if (type == QuicHttpFrameType::DATA || type == QuicHttpFrameType::HEADERS ||
        type == QuicHttpFrameType::PUSH_PROMISE) {
      append_and_clear("QUIC_HTTP_PADDED", QuicHttpFrameFlag::QUIC_HTTP_PADDED);
    }
  }
  if (flags & 0x20) {
    if (type == QuicHttpFrameType::HEADERS) {
      append_and_clear("QUIC_HTTP_PRIORITY",
                       QuicHttpFrameFlag::QUIC_HTTP_PRIORITY);
    }
  }
  if (flags != 0) {
    append_and_clear(QuicStringPrintf("0x%02x", flags), flags);
  }
  DCHECK_EQ(0, flags);
  return s;
}
QuicString QuicHttpFrameFlagsToString(uint8_t type, uint8_t flags) {
  return QuicHttpFrameFlagsToString(static_cast<QuicHttpFrameType>(type),
                                    flags);
}

QuicString QuicHttpErrorCodeToString(uint32_t v) {
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
  return QuicStrCat("UnknownErrorCode(0x", QuicTextUtils::Hex(v), ")");
}
QuicString QuicHttpErrorCodeToString(QuicHttpErrorCode v) {
  return QuicHttpErrorCodeToString(static_cast<uint32_t>(v));
}

QuicString QuicHttpSettingsParameterToString(uint32_t v) {
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
  return QuicStrCat("UnknownSettingsParameter(0x", QuicTextUtils::Hex(v), ")");
}
QuicString QuicHttpSettingsParameterToString(QuicHttpSettingsParameter v) {
  return QuicHttpSettingsParameterToString(static_cast<uint32_t>(v));
}

}  // namespace net
