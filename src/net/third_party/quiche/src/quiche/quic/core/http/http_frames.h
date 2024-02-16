// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/http_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace quic {

enum class HttpFrameType {
  DATA = 0x0,
  HEADERS = 0x1,
  CANCEL_PUSH = 0X3,
  SETTINGS = 0x4,
  PUSH_PROMISE = 0x5,
  GOAWAY = 0x7,
  MAX_PUSH_ID = 0xD,
  // https://tools.ietf.org/html/draft-davidben-http-client-hint-reliability-02
  ACCEPT_CH = 0x89,
  // https://tools.ietf.org/html/draft-ietf-httpbis-priority-03
  PRIORITY_UPDATE_REQUEST_STREAM = 0xF0700,
  // https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-00.html
  WEBTRANSPORT_STREAM = 0x41,
  METADATA = 0x4d,
};

// 7.2.1.  DATA
//
//   DATA frames (type=0x0) convey arbitrary, variable-length sequences of
//   octets associated with an HTTP request or response payload.
struct QUICHE_EXPORT DataFrame {
  absl::string_view data;
};

// 7.2.2.  HEADERS
//
//   The HEADERS frame (type=0x1) is used to carry a header block,
//   compressed using QPACK.
struct QUICHE_EXPORT HeadersFrame {
  absl::string_view headers;
};

// 7.2.4.  SETTINGS
//
//   The SETTINGS frame (type=0x4) conveys configuration parameters that
//   affect how endpoints communicate, such as preferences and constraints
//   on peer behavior

using SettingsMap = absl::flat_hash_map<uint64_t, uint64_t>;

struct QUICHE_EXPORT SettingsFrame {
  SettingsMap values;

  bool operator==(const SettingsFrame& rhs) const {
    return values == rhs.values;
  }

  std::string ToString() const {
    std::string s;
    for (auto it : values) {
      std::string setting = absl::StrCat(
          H3SettingsToString(
              static_cast<Http3AndQpackSettingsIdentifiers>(it.first)),
          " = ", it.second, "; ");
      absl::StrAppend(&s, setting);
    }
    return s;
  }
  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const SettingsFrame& s) {
    os << s.ToString();
    return os;
  }
};

// 7.2.6.  GOAWAY
//
//   The GOAWAY frame (type=0x7) is used to initiate shutdown of a connection by
//   either endpoint.
struct QUICHE_EXPORT GoAwayFrame {
  // When sent from server to client, |id| is a stream ID that should refer to
  // a client-initiated bidirectional stream.
  // When sent from client to server, |id| is a push ID.
  uint64_t id;

  bool operator==(const GoAwayFrame& rhs) const { return id == rhs.id; }
};

// https://httpwg.org/http-extensions/draft-ietf-httpbis-priority.html
//
// The PRIORITY_UPDATE frame specifies the sender-advised priority of a stream.
// Frame type 0xf0700 (called PRIORITY_UPDATE_REQUEST_STREAM in the
// implementation) is used for for request streams.
// Frame type 0xf0701 would be used for push streams but it is not implemented;
// incoming 0xf0701 frames are treated as frames of unknown type.

// Length of a priority frame's first byte.
const QuicByteCount kPriorityFirstByteLength = 1;

struct QUICHE_EXPORT PriorityUpdateFrame {
  uint64_t prioritized_element_id = 0;
  std::string priority_field_value;

  bool operator==(const PriorityUpdateFrame& rhs) const {
    return std::tie(prioritized_element_id, priority_field_value) ==
           std::tie(rhs.prioritized_element_id, rhs.priority_field_value);
  }
  std::string ToString() const {
    return absl::StrCat(
        "Priority Frame : {prioritized_element_id: ", prioritized_element_id,
        ", priority_field_value: ", priority_field_value, "}");
  }

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const PriorityUpdateFrame& s) {
    os << s.ToString();
    return os;
  }
};

// ACCEPT_CH
// https://tools.ietf.org/html/draft-davidben-http-client-hint-reliability-02
//
struct QUICHE_EXPORT AcceptChFrame {
  std::vector<spdy::AcceptChOriginValuePair> entries;

  bool operator==(const AcceptChFrame& rhs) const {
    return entries.size() == rhs.entries.size() &&
           std::equal(entries.begin(), entries.end(), rhs.entries.begin());
  }

  std::string ToString() const {
    std::stringstream s;
    s << *this;
    return s.str();
  }

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const AcceptChFrame& frame) {
    os << "ACCEPT_CH frame with " << frame.entries.size() << " entries: ";
    for (auto& entry : frame.entries) {
      os << "origin: " << entry.origin << "; value: " << entry.value;
    }
    return os;
  }
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_
