// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

QuicConnectionId::QuicConnectionId() : length_(0) {}

QuicConnectionId::QuicConnectionId(const char* data, uint8_t length) {
  if (length > kQuicMaxConnectionIdLength) {
    QUIC_BUG << "Attempted to create connection ID of length " << length;
    length = kQuicMaxConnectionIdLength;
  }
  length_ = length;
  if (length_ > 0) {
    memcpy(data_, data, length_);
  }
}

QuicConnectionId::~QuicConnectionId() {}

const char* QuicConnectionId::data() const {
  return data_;
}

char* QuicConnectionId::mutable_data() {
  return data_;
}

uint8_t QuicConnectionId::length() const {
  return length_;
}

void QuicConnectionId::set_length(uint8_t length) {
  length_ = length;
}

bool QuicConnectionId::IsEmpty() const {
  return length_ == 0;
}

size_t QuicConnectionId::Hash() const {
  uint64_t data_bytes[3] = {0, 0, 0};
  static_assert(sizeof(data_bytes) >= sizeof(data_), "sizeof(data_) changed");
  memcpy(data_bytes, data_, length_);
  // This Hash function is designed to return the same value as the host byte
  // order representation when the connection ID length is 64 bits.
  return QuicEndian::NetToHost64(kQuicDefaultConnectionIdLength ^ length_ ^
                                 data_bytes[0] ^ data_bytes[1] ^ data_bytes[2]);
}

std::string QuicConnectionId::ToString() const {
  if (IsEmpty()) {
    return std::string("0");
  }
  return QuicTextUtils::HexEncode(data_, length_);
}

std::ostream& operator<<(std::ostream& os, const QuicConnectionId& v) {
  os << v.ToString();
  return os;
}

bool QuicConnectionId::operator==(const QuicConnectionId& v) const {
  return length_ == v.length_ && memcmp(data_, v.data_, length_) == 0;
}

bool QuicConnectionId::operator!=(const QuicConnectionId& v) const {
  return !(v == *this);
}

bool QuicConnectionId::operator<(const QuicConnectionId& v) const {
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

static_assert(kQuicDefaultConnectionIdLength == sizeof(uint64_t),
              "kQuicDefaultConnectionIdLength changed");
static_assert(kQuicDefaultConnectionIdLength == PACKET_8BYTE_CONNECTION_ID,
              "kQuicDefaultConnectionIdLength changed");

}  // namespace quic
