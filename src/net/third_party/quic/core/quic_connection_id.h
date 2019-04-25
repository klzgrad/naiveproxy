// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_

#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_uint128.h"

namespace quic {

enum QuicConnectionIdLength {
  PACKET_0BYTE_CONNECTION_ID = 0,
  PACKET_8BYTE_CONNECTION_ID = 8,
};

// This is a property of QUIC headers, it indicates whether the connection ID
// should actually be sent over the wire (or was sent on received packets).
enum QuicConnectionIdIncluded : uint8_t {
  CONNECTION_ID_PRESENT = 1,
  CONNECTION_ID_ABSENT = 2,
};

// Connection IDs can be 0-18 bytes per IETF specifications.
const uint8_t kQuicMaxConnectionIdLength = 18;

// kQuicDefaultConnectionIdLength is the only supported length for QUIC
// versions < v99, and is the default picked for all versions.
const uint8_t kQuicDefaultConnectionIdLength = 8;

class QUIC_EXPORT_PRIVATE QuicConnectionId {
 public:
  // Creates a connection ID of length zero.
  QuicConnectionId();

  // Creates a connection ID from network order bytes.
  QuicConnectionId(const char* data, uint8_t length);

  ~QuicConnectionId();

  // Returns the length of the connection ID, in bytes.
  uint8_t length() const;

  // Sets the length of the connection ID, in bytes.
  void set_length(uint8_t length);

  // Returns a pointer to the connection ID bytes, in network byte order.
  const char* data() const;

  // Returns a mutable pointer to the connection ID bytes,
  // in network byte order.
  char* mutable_data();

  // Returns whether the connection ID has length zero.
  bool IsEmpty() const;

  // Hash() is required to use connection IDs as keys in hash tables.
  size_t Hash() const;

  // Generates an ASCII string that represents
  // the contents of the connection ID, or "0" if it is empty.
  QuicString ToString() const;

  // operator<< allows easily logging connection IDs.
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionId& v);

  bool operator==(const QuicConnectionId& v) const;
  bool operator!=(const QuicConnectionId& v) const;
  // operator< is required to use connection IDs as keys in hash tables.
  bool operator<(const QuicConnectionId& v) const;

 private:
  // The connection ID is represented in network byte order
  // in the first |length_| bytes of |data_|.
  char data_[kQuicMaxConnectionIdLength];
  uint8_t length_;
};

// Creates a connection ID of length zero, unless the restart flag
// quic_connection_ids_network_byte_order is false in which case
// it returns an 8-byte all-zeroes connection ID.
QUIC_EXPORT_PRIVATE QuicConnectionId EmptyQuicConnectionId();

// QuicConnectionIdHash can be passed as hash argument to hash tables.
class QuicConnectionIdHash {
 public:
  size_t operator()(QuicConnectionId const& connection_id) const noexcept {
    return connection_id.Hash();
  }
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_CONNECTION_ID_H_
