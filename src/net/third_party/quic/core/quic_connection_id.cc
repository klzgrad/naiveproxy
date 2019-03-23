// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_connection_id.h"

#include <cstdint>
#include <cstring>
#include <iomanip>

#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_endian.h"
#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"

namespace quic {

QuicConnectionId::QuicConnectionId() {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    id64_ = 0;
    length_ = sizeof(uint64_t);
    return;
  }
  length_ = 0;
}

QuicConnectionId::QuicConnectionId(const char* data, uint8_t length) {
  QUIC_BUG_IF(!QuicConnectionIdUseNetworkByteOrder())
      << "new constructor called when flag disabled";
  if (length > kQuicMaxConnectionIdLength) {
    QUIC_BUG << "Attempted to create connection ID of length " << length;
    length = kQuicMaxConnectionIdLength;
  }
  length_ = length;
  if (length_ > 0) {
    memcpy(data_, data, length_);
  }
  QUIC_RESTART_FLAG_COUNT_N(quic_variable_length_connection_ids_server, 2, 3);
}

QuicConnectionId::QuicConnectionId(uint64_t connection_id64)
    : length_(sizeof(uint64_t)) {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    id64_ = connection_id64;
    return;
  }
  QUIC_BUG_IF(QuicConnectionIdSupportsVariableLength(Perspective::IS_CLIENT) &&
              QuicConnectionIdSupportsVariableLength(Perspective::IS_SERVER))
      << "old constructor called when flag enabled";
  const uint64_t connection_id64_net = QuicEndian::HostToNet64(connection_id64);
  memcpy(&data_, &connection_id64_net, sizeof(connection_id64_net));
}

QuicConnectionId::~QuicConnectionId() {}

uint64_t QuicConnectionId::ToUInt64() const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return id64_;
  }
  QUIC_BUG_IF(QuicConnectionIdSupportsVariableLength(Perspective::IS_CLIENT) &&
              QuicConnectionIdSupportsVariableLength(Perspective::IS_SERVER))
      << "ToUInt64 called when flag enabled";
  uint64_t connection_id64_net = 0;
  memcpy(&connection_id64_net, &data_,
         std::min<size_t>(static_cast<size_t>(length_),
                          sizeof(connection_id64_net)));
  return QuicEndian::NetToHost64(connection_id64_net);
}

const char* QuicConnectionId::data() const {
  QUIC_BUG_IF(!QuicConnectionIdUseNetworkByteOrder())
      << "data called when flag disabled";
  QUIC_RESTART_FLAG_COUNT_N(quic_variable_length_connection_ids_server, 3, 3);
  return data_;
}

char* QuicConnectionId::mutable_data() {
  QUIC_BUG_IF(!QuicConnectionIdUseNetworkByteOrder())
      << "mutable_data called when flag disabled";
  return data_;
}

uint8_t QuicConnectionId::length() const {
  return length_;
}

void QuicConnectionId::set_length(uint8_t length) {
  QUIC_BUG_IF(!QuicConnectionIdUseNetworkByteOrder())
      << "set_length called when flag disabled";
  length_ = length;
}

bool QuicConnectionId::IsEmpty() const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return id64_ == 0;
  }
  return length_ == 0;
}

size_t QuicConnectionId::Hash() const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return id64_;
  }
  uint64_t data_bytes[3] = {0, 0, 0};
  static_assert(sizeof(data_bytes) >= sizeof(data_), "sizeof(data_) changed");
  memcpy(data_bytes, data_, length_);
  // This Hash function is designed to return the same value
  // as ToUInt64() when the connection ID length is 64 bits.
  return QuicEndian::NetToHost64(kQuicDefaultConnectionIdLength ^ length_ ^
                                 data_bytes[0] ^ data_bytes[1] ^ data_bytes[2]);
}

QuicString QuicConnectionId::ToString() const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return QuicTextUtils::Uint64ToString(id64_);
  }
  if (IsEmpty()) {
    return QuicString("0");
  }
  return QuicTextUtils::HexEncode(data_, length_);
}

std::ostream& operator<<(std::ostream& os, const QuicConnectionId& v) {
  os << v.ToString();
  return os;
}

bool QuicConnectionId::operator==(const QuicConnectionId& v) const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return id64_ == v.id64_;
  }
  return length_ == v.length_ && memcmp(data_, v.data_, length_) == 0;
}

bool QuicConnectionId::operator!=(const QuicConnectionId& v) const {
  return !(v == *this);
}

bool QuicConnectionId::operator<(const QuicConnectionId& v) const {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return id64_ < v.id64_;
  }
  if (length_ < v.length_) {
    return true;
  }
  if (length_ > v.length_) {
    return false;
  }
  return memcmp(data_, v.data_, length_) < 0;
}

QuicConnectionId EmptyQuicConnectionId() {
  return QuicConnectionId();
}

QuicConnectionId QuicConnectionIdFromUInt64(uint64_t connection_id64) {
  return QuicConnectionId(connection_id64);
}

uint64_t QuicConnectionIdToUInt64(QuicConnectionId connection_id) {
  return connection_id.ToUInt64();
}

bool QuicConnectionIdUseNetworkByteOrder() {
  const bool res = GetQuicRestartFlag(quic_connection_ids_network_byte_order);
  if (res) {
    QUIC_RESTART_FLAG_COUNT(quic_connection_ids_network_byte_order);
  }
  return res;
}

bool QuicConnectionIdSupportsVariableLength(Perspective perspective) {
  if (!QuicConnectionIdUseNetworkByteOrder()) {
    return false;
  }
  bool res;
  if (perspective == Perspective::IS_SERVER) {
    res = GetQuicRestartFlag(quic_variable_length_connection_ids_server);
    if (res) {
      QUIC_RESTART_FLAG_COUNT_N(quic_variable_length_connection_ids_server, 1,
                                3);
    }
  } else {
    res = GetQuicRestartFlag(quic_variable_length_connection_ids_client);
    if (res) {
      QUIC_RESTART_FLAG_COUNT(quic_variable_length_connection_ids_client);
    }
  }
  return res;
}

static_assert(kQuicDefaultConnectionIdLength == sizeof(uint64_t),
              "kQuicDefaultConnectionIdLength changed");
static_assert(kQuicDefaultConnectionIdLength == PACKET_8BYTE_CONNECTION_ID,
              "kQuicDefaultConnectionIdLength changed");

}  // namespace quic
