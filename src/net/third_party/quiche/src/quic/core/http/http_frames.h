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
#include "quic/core/http/http_constants.h"
#include "quic/core/quic_types.h"

namespace quic {

enum class HttpFrameType {
  DATA = 0x0,
  HEADERS = 0x1,
  CANCEL_PUSH = 0X3,
  SETTINGS = 0x4,
  PUSH_PROMISE = 0x5,
  GOAWAY = 0x7,
  MAX_PUSH_ID = 0xD,
  // https://tools.ietf.org/html/draft-ietf-httpbis-priority-01
  PRIORITY_UPDATE = 0XF,
  // https://tools.ietf.org/html/draft-davidben-http-client-hint-reliability-02
  ACCEPT_CH = 0x89,
  // https://tools.ietf.org/html/draft-ietf-httpbis-priority-02
  PRIORITY_UPDATE_REQUEST_STREAM = 0xF0700,
};

// 7.2.1.  DATA
//
//   DATA frames (type=0x0) convey arbitrary, variable-length sequences of
//   octets associated with an HTTP request or response payload.
struct QUIC_EXPORT_PRIVATE DataFrame {
  absl::string_view data;
};

// 7.2.2.  HEADERS
//
//   The HEADERS frame (type=0x1) is used to carry a header block,
//   compressed using QPACK.
struct QUIC_EXPORT_PRIVATE HeadersFrame {
  absl::string_view headers;
};

// 7.2.3.  CANCEL_PUSH
//
//   The CANCEL_PUSH frame (type=0x3) is used to request cancellation of
//   server push prior to the push stream being created.
using PushId = uint64_t;

struct QUIC_EXPORT_PRIVATE CancelPushFrame {
  PushId push_id;

  bool operator==(const CancelPushFrame& rhs) const {
    return push_id == rhs.push_id;
  }
};

// 7.2.4.  SETTINGS
//
//   The SETTINGS frame (type=0x4) conveys configuration parameters that
//   affect how endpoints communicate, such as preferences and constraints
//   on peer behavior

using SettingsMap = absl::flat_hash_map<uint64_t, uint64_t>;

struct QUIC_EXPORT_PRIVATE SettingsFrame {
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
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const SettingsFrame& s) {
    os << s.ToString();
    return os;
  }
};

// 7.2.5.  PUSH_PROMISE
//
//   The PUSH_PROMISE frame (type=0x05) is used to carry a request header
//   set from server to client, as in HTTP/2.
struct QUIC_EXPORT_PRIVATE PushPromiseFrame {
  PushId push_id;
  absl::string_view headers;

  bool operator==(const PushPromiseFrame& rhs) const {
    return push_id == rhs.push_id && headers == rhs.headers;
  }
};

// 7.2.6.  GOAWAY
//
//   The GOAWAY frame (type=0x7) is used to initiate shutdown of a connection by
//   either endpoint.
struct QUIC_EXPORT_PRIVATE GoAwayFrame {
  // When sent from server to client, |id| is a stream ID that should refer to
  // a client-initiated bidirectional stream.
  // When sent from client to server, |id| is a push ID.
  uint64_t id;

  bool operator==(const GoAwayFrame& rhs) const { return id == rhs.id; }
};

// 7.2.7.  MAX_PUSH_ID
//
//   The MAX_PUSH_ID frame (type=0xD) is used by clients to control the
//   number of server pushes that the server can initiate.
struct QUIC_EXPORT_PRIVATE MaxPushIdFrame {
  PushId push_id;

  bool operator==(const MaxPushIdFrame& rhs) const {
    return push_id == rhs.push_id;
  }
};

// https://httpwg.org/http-extensions/draft-ietf-httpbis-priority.html
//
// The PRIORITY_UPDATE frame specifies the sender-advised priority of a stream.
// https://tools.ietf.org/html/draft-ietf-httpbis-priority-01 uses frame type
// 0x0f, both for request streams and push streams.
// https://tools.ietf.org/html/draft-ietf-httpbis-priority-02 uses frame types
// 0xf0700 for request streams and 0xf0701 for push streams (not implemented).

// Length of a priority frame's first byte.
const QuicByteCount kPriorityFirstByteLength = 1;

enum PrioritizedElementType : uint8_t {
  REQUEST_STREAM = 0x00,
  PUSH_STREAM = 0x80,
};

struct QUIC_EXPORT_PRIVATE PriorityUpdateFrame {
  PrioritizedElementType prioritized_element_type = REQUEST_STREAM;
  uint64_t prioritized_element_id = 0;
  std::string priority_field_value;

  bool operator==(const PriorityUpdateFrame& rhs) const {
    return std::tie(prioritized_element_type, prioritized_element_id,
                    priority_field_value) ==
           std::tie(rhs.prioritized_element_type, rhs.prioritized_element_id,
                    rhs.priority_field_value);
  }
  std::string ToString() const {
    return absl::StrCat("Priority Frame : {prioritized_element_type: ",
                        static_cast<int>(prioritized_element_type),
                        ", prioritized_element_id: ", prioritized_element_id,
                        ", priority_field_value: ", priority_field_value, "}");
  }

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const PriorityUpdateFrame& s) {
    os << s.ToString();
    return os;
  }
};

// ACCEPT_CH
// https://tools.ietf.org/html/draft-davidben-http-client-hint-reliability-02
//
struct QUIC_EXPORT_PRIVATE AcceptChFrame {
  struct QUIC_EXPORT_PRIVATE OriginValuePair {
    std::string origin;
    std::string value;
    bool operator==(const OriginValuePair& rhs) const {
      return origin == rhs.origin && value == rhs.value;
    }
  };

  std::vector<OriginValuePair> entries;

  bool operator==(const AcceptChFrame& rhs) const {
    return entries.size() == rhs.entries.size() &&
           std::equal(entries.begin(), entries.end(), rhs.entries.begin());
  }

  std::string ToString() const {
    std::stringstream s;
    s << *this;
    return s.str();
  }

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
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
