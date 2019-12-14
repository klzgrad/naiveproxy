// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_

#include <cstdint>
#include <map>
#include <ostream>

#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

enum class HttpFrameType : uint8_t {
  DATA = 0x0,
  HEADERS = 0x1,
  PRIORITY = 0X2,
  CANCEL_PUSH = 0X3,
  SETTINGS = 0x4,
  PUSH_PROMISE = 0x5,
  GOAWAY = 0x7,
  MAX_PUSH_ID = 0xD,
  DUPLICATE_PUSH = 0xE
};

// 4.2.1.  DATA
//
//   DATA frames (type=0x0) convey arbitrary, variable-length sequences of
//   octets associated with an HTTP request or response payload.
struct DataFrame {
  QuicStringPiece data;
};

// 4.2.2.  HEADERS
//
//   The HEADERS frame (type=0x1) is used to carry a header block,
//   compressed using QPACK.
struct HeadersFrame {
  QuicStringPiece headers;
};

// 4.2.3.  PRIORITY
//
//   The PRIORITY (type=0x02) frame specifies the sender-advised priority
//   of a stream

// Length of the weight field of a priority frame.
const QuicByteCount kPriorityWeightLength = 1;
// Length of a priority frame's first byte.
const QuicByteCount kPriorityFirstByteLength = 1;
// The bit that indicates Priority frame is exclusive.
const uint8_t kPriorityExclusiveBit = 8;

enum PriorityElementType : uint8_t {
  REQUEST_STREAM = 0,
  PUSH_STREAM = 1,
  PLACEHOLDER = 2,
  ROOT_OF_TREE = 3
};

struct PriorityFrame {
  PriorityElementType prioritized_type = REQUEST_STREAM;
  PriorityElementType dependency_type = REQUEST_STREAM;
  bool exclusive = false;
  uint64_t prioritized_element_id = 0;
  uint64_t element_dependency_id = 0;
  uint8_t weight = 0;

  bool operator==(const PriorityFrame& rhs) const {
    return prioritized_type == rhs.prioritized_type &&
           dependency_type == rhs.dependency_type &&
           exclusive == rhs.exclusive &&
           prioritized_element_id == rhs.prioritized_element_id &&
           element_dependency_id == rhs.element_dependency_id &&
           weight == rhs.weight;
  }
  std::string ToString() const {
    return QuicStrCat("Priority Frame : {prioritized_type: ", prioritized_type,
                      ", dependency_type: ", dependency_type,
                      ", exclusive: ", exclusive,
                      ", prioritized_element_id: ", prioritized_element_id,
                      ", element_dependency_id: ", element_dependency_id,
                      ", weight: ", weight, "}");
  }

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const PriorityFrame& s) {
    os << s.ToString();
    return os;
  }
};

// 4.2.4.  CANCEL_PUSH
//
//   The CANCEL_PUSH frame (type=0x3) is used to request cancellation of
//   server push prior to the push stream being created.
using PushId = uint64_t;

struct CancelPushFrame {
  PushId push_id;

  bool operator==(const CancelPushFrame& rhs) const {
    return push_id == rhs.push_id;
  }
};

// 4.2.5.  SETTINGS
//
//   The SETTINGS frame (type=0x4) conveys configuration parameters that
//   affect how endpoints communicate, such as preferences and constraints
//   on peer behavior

using SettingsMap = std::map<uint64_t, uint64_t>;

struct SettingsFrame {
  SettingsMap values;

  bool operator==(const SettingsFrame& rhs) const {
    return values == rhs.values;
  }

  std::string ToString() const {
    std::string s;
    for (auto it : values) {
      std::string setting = QuicStrCat(
          SpdyUtils::H3SettingsToString(
              static_cast<Http3AndQpackSettingsIdentifiers>(it.first)),
          " = ", it.second, "; ");
      QuicStrAppend(&s, setting);
    }
    return s;
  }
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const SettingsFrame& s) {
    os << s.ToString();
    return os;
  }
};

// 4.2.6.  PUSH_PROMISE
//
//   The PUSH_PROMISE frame (type=0x05) is used to carry a request header
//   set from server to client, as in HTTP/2.
struct PushPromiseFrame {
  PushId push_id;
  QuicStringPiece headers;

  bool operator==(const PushPromiseFrame& rhs) const {
    return push_id == rhs.push_id && headers == rhs.headers;
  }
};

// 4.2.7.  GOAWAY
//
//   The GOAWAY frame (type=0x7) is used to initiate graceful shutdown of
//   a connection by a server.
struct GoAwayFrame {
  QuicStreamId stream_id;

  bool operator==(const GoAwayFrame& rhs) const {
    return stream_id == rhs.stream_id;
  }
};

// 4.2.8.  MAX_PUSH_ID
//
//   The MAX_PUSH_ID frame (type=0xD) is used by clients to control the
//   number of server pushes that the server can initiate.
struct MaxPushIdFrame {
  PushId push_id;

  bool operator==(const MaxPushIdFrame& rhs) const {
    return push_id == rhs.push_id;
  }
};

// 4.2.9.  DUPLICATE_PUSH
//
//  The DUPLICATE_PUSH frame (type=0xE) is used by servers to indicate
//  that an existing pushed resource is related to multiple client
//  requests.
struct DuplicatePushFrame {
  PushId push_id;

  bool operator==(const DuplicatePushFrame& rhs) const {
    return push_id == rhs.push_id;
  }
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_FRAMES_H_
