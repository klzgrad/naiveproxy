// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/http2_constants.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

std::string Http2FrameTypeToString(Http2FrameType v) {
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
    case Http2FrameType::PRIORITY_UPDATE:
      return "PRIORITY_UPDATE";
  }
  return absl::StrCat("UnknownFrameType(", static_cast<int>(v), ")");
}

std::string Http2FrameTypeToString(uint8_t v) {
  return Http2FrameTypeToString(static_cast<Http2FrameType>(v));
}

std::string Http2FrameFlagsToString(Http2FrameType type, uint8_t flags) {
  std::string s;
  // Closure to append flag name |v| to the std::string |s|,
  // and to clear |bit| from |flags|.
  auto append_and_clear = [&s, &flags](absl::string_view v, uint8_t bit) {
    if (!s.empty()) {
      s.push_back('|');
    }
    absl::StrAppend(&s, v);
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
    append_and_clear(absl::StrFormat("0x%02x", flags), flags);
  }
  QUICHE_DCHECK_EQ(0, flags);
  return s;
}
std::string Http2FrameFlagsToString(uint8_t type, uint8_t flags) {
  return Http2FrameFlagsToString(static_cast<Http2FrameType>(type), flags);
}

std::string Http2ErrorCodeToString(uint32_t v) {
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
  return absl::StrCat("UnknownErrorCode(0x", absl::Hex(v), ")");
}
std::string Http2ErrorCodeToString(Http2ErrorCode v) {
  return Http2ErrorCodeToString(static_cast<uint32_t>(v));
}

std::string Http2SettingsParameterToString(uint32_t v) {
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
  return absl::StrCat("UnknownSettingsParameter(0x", absl::Hex(v), ")");
}
std::string Http2SettingsParameterToString(Http2SettingsParameter v) {
  return Http2SettingsParameterToString(static_cast<uint32_t>(v));
}

// Invalid HTTP/2 header names according to
// https://datatracker.ietf.org/doc/html/rfc7540#section-8.1.2.2.
// TODO(b/78024822): Consider adding "upgrade" to this set.
constexpr char const* kHttp2InvalidHeaderNames[] = {
    "connection",        "host", "keep-alive", "proxy-connection",
    "transfer-encoding", "",
};

constexpr char const* kHttp2InvalidHeaderNamesOld[] = {
    "connection", "host", "keep-alive", "proxy-connection", "transfer-encoding",
};

const InvalidHeaderSet& GetInvalidHttp2HeaderSet() {
  if (!GetQuicheReloadableFlag(quic, quic_verify_request_headers_2)) {
    static const auto* invalid_header_set_old =
        new InvalidHeaderSet(std::begin(http2::kHttp2InvalidHeaderNamesOld),
                             std::end(http2::kHttp2InvalidHeaderNamesOld));
    return *invalid_header_set_old;
  }
  QUICHE_RELOADABLE_FLAG_COUNT_N(quic_verify_request_headers_2, 3, 3);
  static const auto* invalid_header_set =
      new InvalidHeaderSet(std::begin(http2::kHttp2InvalidHeaderNames),
                           std::end(http2::kHttp2InvalidHeaderNames));
  return *invalid_header_set;
}

}  // namespace http2
